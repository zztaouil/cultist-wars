"""Shared policy/value training. CPU, single CUDA, or torchrun DDP (two T4s)."""
import argparse,copy,hashlib,json,os,time
from pathlib import Path
import numpy as np
import torch
from torch import nn
import torch.distributed as dist

class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.trunk0=nn.Linear(208,64);self.trunk1=nn.Linear(64,32)
        self.action=nn.Linear(32,32);self.policy=nn.Linear(32,1);self.value=nn.Linear(32,1)
    def forward(self,s,a):
        z=self.trunk1(self.trunk0(s).relu()).relu()
        return self.policy((z[:,None,:]+self.action(a)).relu()).squeeze(-1),self.value(z).tanh().squeeze(-1)

def main():
    p=argparse.ArgumentParser();p.add_argument('--cache',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--epochs',type=int,default=12);p.add_argument('--batch-size',type=int,default=256)
    p.add_argument('--device',default='cpu');p.add_argument('--threads',type=int,default=4);p.add_argument('--seed',type=int,default=31)
    p.add_argument('--lr',type=float,default=.001);args=p.parse_args()
    world=int(os.environ.get('WORLD_SIZE','1'));rank=int(os.environ.get('RANK','0'));local=int(os.environ.get('LOCAL_RANK','0'))
    if world>1:
        torch.cuda.set_device(local);dist.init_process_group('nccl');device=torch.device('cuda',local)
    else:device=torch.device(args.device)
    torch.manual_seed(args.seed);torch.set_num_threads(args.threads);rng=np.random.default_rng(args.seed)
    if rank==0:args.out.mkdir(parents=True,exist_ok=False)
    if world>1:dist.barrier()
    data=np.load(args.cache/'targets.npz');s=np.load(args.cache/'states.npy',mmap_mode='r');a=np.memmap(args.cache/'actions.f32',dtype='<f4',mode='r').reshape(-1,32)
    off=data['offsets'];y=data['labels'];v=data['values'];split=data['splits'];length=np.diff(off)
    assert len(s)==len(y)==len(split) and len(a)==off[-1] and np.all((y>=0)&(y<length))
    records=[json.loads(r) for r in (args.cache/'records.jsonl').read_text().splitlines()];groups={}
    assert len(records)==len(y)
    for rec,sp in zip(records,split):groups.setdefault(rec['map'],set()).add(int(sp))
    assert all(len(g)==1 for g in groups.values())
    ids=[np.flatnonzero(split==i) for i in range(3)];assert all(len(i) for i in ids)
    model=Net().to(device);raw=model
    if world>1:model=nn.parallel.DistributedDataParallel(model,device_ids=[local])
    opt=torch.optim.AdamW(model.parameters(),lr=args.lr,weight_decay=1e-4)
    def batch(ii):
        width=int(length[ii].max());aa=np.zeros((len(ii),width,32),dtype=np.float32)
        mask=np.arange(width)[None,:]<length[ii,None]
        for j,k in enumerate(ii):aa[j,:length[k]]=a[off[k]:off[k+1]]
        return (torch.from_numpy(np.array(s[ii])).to(device),torch.from_numpy(aa).to(device),torch.from_numpy(mask).to(device),torch.from_numpy(y[ii]).to(device),torch.from_numpy(v[ii]).to(device))
    @torch.no_grad()
    def evaluate(ii):
        raw.eval();ce=mse=acc=0.
        for start in range(0,len(ii),args.batch_size):
            ss,aa,mask,yy,vv=batch(ii[start:start+args.batch_size]);logits,pred=raw(ss,aa);logits=logits.masked_fill(~mask,-1e9)
            ce+=nn.functional.cross_entropy(logits,yy,reduction='sum').item();mse+=((pred-vv)**2).sum().item();acc+=(logits.argmax(1)==yy).sum().item()
        return dict(policy_loss=ce/len(ii),value_mse=mse/len(ii),top1=acc/len(ii),positions=len(ii),loss=(ce+mse)/len(ii))
    best=float('inf');best_state=None;history=[];start=time.monotonic()
    for epoch in range(1,args.epochs+1):
        model.train();order=rng.permutation(ids[0]);bs=args.batch_size*world
        # Pad to equal full batches so every DDP rank executes same collectives.
        need=(-len(order))%bs
        if need:order=np.concatenate([order,np.resize(order,need)])
        for k in range(0,len(order),bs):
            ii=order[k+rank*args.batch_size:k+(rank+1)*args.batch_size]
            ss,aa,mask,yy,vv=batch(ii);opt.zero_grad(set_to_none=True)
            logits,pred=model(ss,aa);loss=nn.functional.cross_entropy(logits.masked_fill(~mask,-1e9),yy)+nn.functional.mse_loss(pred,vv)
            assert torch.isfinite(loss);loss.backward();opt.step()
        if world>1:dist.barrier()
        if rank==0:
            metrics=evaluate(ids[1]);row=dict(epoch=epoch,seconds=time.monotonic()-start,validation=metrics);history.append(row);print(json.dumps(row),flush=True)
            if metrics['loss']<best:
                best=metrics['loss'];best_state=copy.deepcopy(raw.state_dict());torch.save(dict(state_dict=best_state,feature_version=1,epoch=epoch),args.out/'best.pt')
            (args.out/'history.json').write_text(json.dumps(history,indent=2))
        if world>1:dist.barrier()
    if rank==0:
        raw.load_state_dict(best_state)
        report=dict(validation=evaluate(ids[1]),test=evaluate(ids[2]),parameters=sum(p.numel() for p in raw.parameters()),seconds=time.monotonic()-start,world_size=world,config={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},dataset=json.loads((args.cache/'report.json').read_text()),torch_version=torch.__version__,targets_sha256=hashlib.sha256((args.cache/'targets.npz').read_bytes()).hexdigest(),notes=['Checkpoint selected by validation policy CE + value MSE.','Outcome labels from expert-game distribution; no search-leaf/self-play training yet.'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
    if world>1:dist.destroy_process_group()
if __name__=='__main__':main()
