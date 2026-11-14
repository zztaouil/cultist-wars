"""Train a grouped-action imitation policy; write portable float weights and report."""
import argparse, copy, json, pathlib, random, time
import numpy as np
import torch
from torch import nn
p=argparse.ArgumentParser();p.add_argument('--minutes',type=float,default=12);p.add_argument('--epochs',type=int,default=80);p.add_argument('--device',default='cuda:0');p.add_argument('--seed',type=int,default=17);args=p.parse_args()
ROOT=pathlib.Path(__file__).resolve().parent
random.seed(args.seed);np.random.seed(args.seed);torch.manual_seed(args.seed);torch.set_num_threads(4)
device=torch.device(args.device);t0=time.monotonic()
m=json.loads((ROOT/'cache/meta.json').read_text());x=np.fromfile(ROOT/'cache/features.f32',dtype='<f4').reshape(-1,128);o=np.fromfile(ROOT/'cache/orders.i32',dtype='<i4');off=np.array(m['offsets']);lens=np.diff(off);y=np.array(m['labels']);split=np.array(m['splits'])
assert np.isfinite(x).all() and all(y<lens)
# Cache padded tensors once. Padding is masked before each softmax.
n=len(y);width=int(lens.max());xx=np.zeros((n,width,128),np.float32);oo=np.full((n,width),-1e9,np.float32)
for i in range(n):xx[i,:lens[i]]=x[off[i]:off[i+1]];oo[i,:lens[i]]=o[off[i]:off[i+1]]
X=torch.from_numpy(xx).to(device);Y=torch.tensor(y,device=device);L=torch.tensor(lens,device=device);O=torch.from_numpy(oo).to(device);slots=torch.arange(width,device=device)[None,:]
ids=[torch.tensor(np.flatnonzero(split==i),device=device) for i in range(3)]
model=nn.Sequential(nn.Linear(128,32),nn.ReLU(),nn.Linear(32,1)).to(device)
opt=torch.optim.AdamW(model.parameters(),lr=0.001,weight_decay=0.0001)
def logits(ii):return model(X[ii]).squeeze(-1).masked_fill(slots>=L[ii,None],-1e9)
@torch.no_grad()
def evaluate(ii,baseline=False):
 loss=top1=top3=count=0
 for jj in ii.split(256):
  z=O[jj] if baseline else logits(jj);target=Y[jj];count+=len(jj)
  if not baseline:loss+=nn.functional.cross_entropy(z,target,reduction='sum').item()
  top1+=(z.argmax(1)==target).sum().item();top3+=(z.topk(min(3,width),dim=1).indices==target[:,None]).any(1).sum().item()
 return {'loss':None if baseline else loss/count,'top1':top1/count,'top3':top3/count,'positions':count}
best=float('inf');best_state=None;stale=0;history=[];best_epoch=0
for epoch in range(1,args.epochs+1):
 start=time.monotonic();model.train();trainids=ids[0][torch.randperm(len(ids[0]),device=device)];total=0
 for jj in trainids.split(256):
  opt.zero_grad(set_to_none=True);loss=nn.functional.cross_entropy(logits(jj),Y[jj]);loss.backward();opt.step();total+=loss.item()*len(jj)
 model.eval();v=evaluate(ids[1]);row={'epoch':epoch,'train_loss':total/len(ids[0]),'validation':v,'seconds':round(time.monotonic()-start,2)};history.append(row);print(json.dumps(row),flush=True)
 if v['loss']<best-0.0001:best=v['loss'];best_state=copy.deepcopy(model.state_dict());best_epoch=epoch;stale=0
 else:stale+=1
 if stale>=8 or time.monotonic()-t0>=args.minutes*60:break
assert best_state is not None
model.load_state_dict(best_state);model.eval();out=ROOT/'output';out.mkdir(exist_ok=True)
weights={k:v.cpu().numpy() for k,v in model.state_dict().items()};np.savez(out/'policy_weights.npz',**weights)
torch.save({'state_dict':{k:v.cpu() for k,v in best_state.items()},'feature_version':1,'architecture':[128,32,1],'baseline_sha256':m['baseline_sha256']},out/'policy.pt')
# Portable float export: C integration/quantization and equal-time matches are next stage.
(out/'policy_weights.json').write_text(json.dumps({k:v.tolist() for k,v in weights.items()}))
report={'baseline':'41710886 (archived identical 41709358.c)','baseline_sha256':m['baseline_sha256'],'feature_version':1,'parameters':sum(v.numel() for v in model.parameters()),'seed':args.seed,'device':str(device),'gpu':torch.cuda.get_device_name(device) if device.type=='cuda' else None,'best_epoch':best_epoch,'epochs':len(history),'seconds':round(time.monotonic()-t0,1),'dataset':json.loads((ROOT/'cache/report.json').read_text()),'validation':evaluate(ids[1]),'test':evaluate(ids[2]),'static_order_test':evaluate(ids[2],True),'notes':['Static ordering excludes history and TT; not a search-strength comparison.','Test split used only after checkpoint selection.','No C deployment or match-strength gain established.']}
(out/'report.json').write_text(json.dumps(report,indent=2));(out/'history.json').write_text(json.dumps(history,indent=2));print('=== PASTE BACK START ===');print(json.dumps(report,indent=2));print('=== PASTE BACK END ===')
