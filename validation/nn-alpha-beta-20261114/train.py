"""Train a compact full-state outcome evaluator; export packed standalone C++ weights."""
import argparse, base64, copy, hashlib, json
from pathlib import Path
import numpy as np
import torch
from torch import nn
ROOT=Path(__file__).resolve().parent
class Net(nn.Sequential):
    def __init__(self):
        super().__init__(nn.Linear(239,32),nn.ReLU(),nn.Linear(32,16),nn.ReLU(),nn.Linear(16,1),nn.Tanh())
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--epochs',type=int,default=20);args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);torch.manual_seed(73);rng=np.random.default_rng(73)
    old=ROOT.parent/'nn-mcts-20261114/cache'
    records=[json.loads(r) for r in (old/'records.jsonl').read_text().splitlines()]
    lookup={(r['game'],r['turn']):i for i,r in enumerate(records)}
    x=np.zeros((len(records),239),np.float32);x[:,:208]=np.load(old/'states.npy');seen=set()
    # Recover exact RNG from the already verified replay corpus. Match every old feature.
    for line in (ROOT.parent/'imitation-1m-20261114/build/recon.txt').open():
        if line.startswith('G '):_,gid,ws=line.split();turn=0
        elif line.startswith('S '):
            s,a=json.loads('['+line[2:]+']');idx=lookup.get((gid,turn));turn+=1
            if idx is None:continue
            assert np.array_equal(x[idx,:91],np.array(list(ws),np.float32))
            for i in range(14):
                pos,hp,owner,leader=s[4*i:4*i+4];k=91+8*i
                expect=[pos%13/12,pos//13/6,hp/10,owner==s[56],owner==1-s[56],owner==2,leader] if hp else [0]*7
                assert np.allclose(x[idx,k:k+7],expect)
                assert x[idx,k+7]==((s[59]>>i)&1)
            assert abs(x[idx,203]-s[57]/150)<1e-6 and x[idx,204]==s[56] and x[idx,205]==s[60]
            x[idx,208:]=[(s[58]>>b)&1 if s[60] else 0 for b in range(31)];seen.add(idx)
            if len(seen)==len(records):break
    assert len(seen)==len(records)
    data=np.load(old/'targets.npz');y=data['values'];split=data['splits'];ids=[np.flatnonzero(split==i) for i in range(3)]
    groups={}
    for rec,sp in zip(records,split):groups.setdefault(rec['map'],set()).add(int(sp))
    assert all(len(g)==1 for g in groups.values())
    np.savez(args.out/'data.npz',x=x,y=y,split=split)
    xt=torch.from_numpy(x);yt=torch.from_numpy(y[:,None]);model=Net();opt=torch.optim.AdamW(model.parameters(),lr=.001,weight_decay=.001)
    @torch.no_grad()
    def mse(ii):return ((model(xt[ii])-yt[ii])**2).mean().item()
    best=float('inf');history=[]
    for epoch in range(args.epochs):
        for ii in np.array_split(rng.permutation(ids[0]),max(1,len(ids[0])//512)):
            batch=xt[ii].clone();unknown=torch.rand(len(ii))<.5
            batch[unknown,205]=0;batch[unknown,208:]=0
            opt.zero_grad();loss=((model(batch)-yt[ii])**2).mean();loss.backward();opt.step()
        v=mse(ids[1]);history.append(dict(epoch=epoch+1,validation_mse=v));print(json.dumps(history[-1]),flush=True)
        if v<best:best=v;state=copy.deepcopy(model.state_dict());best_epoch=epoch+1
    model.load_state_dict(state);torch.save(state,args.out/'best.pt')
    # Deployment uses IEEE binary16 storage and float32 arithmetic.
    params=np.concatenate([p.detach().numpy().ravel() for p in model.parameters()]).astype('<f2')
    assert np.isfinite(params).all()
    blob=base64.b64encode(params.tobytes()).decode()
    (args.out/'weights.inc').write_text('static const char value_blob[]=\n'+''.join('"'+blob[i:i+120]+'"\n' for i in range(0,len(blob),120))+';\n')
    with torch.no_grad():
        for p in model.parameters():p.copy_(p.to(torch.float16).float())
    report=dict(features=239,architecture=[239,32,16,1],parameters=len(params),best_epoch=best_epoch,validation_mse=mse(ids[1]),test_mse=mse(ids[2]),test_zero_mse=float(np.mean(y[ids[2]]**2)),split_positions=[len(i) for i in ids],history=history,torch_version=torch.__version__,data_sha256=hashlib.sha256((args.out/'data.npz').read_bytes()).hexdigest(),baseline_sha256=hashlib.sha256((ROOT/'baseline.cpp').read_bytes()).hexdigest())
    (args.out/'report.json').write_text(json.dumps(report,indent=2))
    np.save(args.out/'predictions.npy',model(xt).detach().numpy().ravel())
    print(json.dumps({k:v for k,v in report.items() if k!='history'}))
if __name__=='__main__':main()
