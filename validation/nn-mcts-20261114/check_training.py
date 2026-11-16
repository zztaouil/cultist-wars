"""Evaluate deployed FP16 model, outcome baselines, and map overlap."""
import json
import numpy as np
import torch
from train import Net
from prepare import map_key
from bridge import ROOT

torch.set_num_threads(2)
d=np.load(ROOT/'cache/targets.npz');x=np.load(ROOT/'cache/states.npy',mmap_mode='r');a=np.memmap(ROOT/'cache/actions.f32',mode='r',dtype='<f4').reshape(-1,32)
y=d['labels'];v=d['values'];off=d['offsets'];splits=d['splits'];lens=np.diff(off)
model=Net();packed=np.load(ROOT/'packed.npy');cursor=0
with torch.no_grad():
 for p in model.parameters():
  n=p.numel();p.copy_(torch.from_numpy(packed[cursor:cursor+n].reshape(p.shape)));cursor+=n
model.eval();report={}
with torch.no_grad():
 for split,name in [(1,'validation'),(2,'test')]:
  ids=np.flatnonzero(splits==split);ce=mse=acc=0
  for start in range(0,len(ids),256):
   ii=ids[start:start+256];width=int(lens[ii].max());aa=np.zeros((len(ii),width,32),np.float32)
   for j,k in enumerate(ii):aa[j,:lens[k]]=a[off[k]:off[k+1]]
   mask=torch.from_numpy(np.arange(width)[None,:]<lens[ii,None])
   logits,value=model(torch.from_numpy(np.array(x[ii])),torch.from_numpy(aa));logits=logits.masked_fill(~mask,-1e9)
   ce+=torch.nn.functional.cross_entropy(logits,torch.from_numpy(y[ii]),reduction='sum').item()
   mse+=((value-torch.from_numpy(v[ii]))**2).sum().item();acc+=(logits.argmax(1)==torch.from_numpy(y[ii])).sum().item()
  report[name]=dict(positions=len(ids),policy_loss=ce/len(ids),value_mse=mse/len(ids),top1=acc/len(ids),zero_value_mse=float(np.mean(v[ids]**2)),train_mean_value_mse=float(np.mean((v[ids]-v[splits==0].mean())**2)))
records=[json.loads(l) for l in (ROOT/'cache/records.jsonl').read_text().splitlines()];training_maps={r['map'] for r in records}
screen=json.loads((ROOT/'screen/manifest.json').read_text());report['screen_map_overlap_with_pilot_corpus']=sum(map_key(w) in training_maps for _,w,_ in screen['maps'])
(ROOT/'training-check.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
