"""Evaluate archived policy on exactly this experiment's original test positions."""
import hashlib,json
from pathlib import Path
import numpy as np
import torch
from torch import nn
ROOT=Path(__file__).resolve().parent;cache=ROOT.parent/'imitation-5m-20261114/cache'
torch.set_num_threads(2)
m=json.loads((cache/'meta.json').read_text());off=np.asarray(m['offsets']);lengths=np.diff(off);y=np.asarray(m['labels']);splits=np.asarray(m['splits'])
x=np.memmap(cache/'features.f32',dtype='<f4',mode='r').reshape(-1,128)
previous=ROOT.parent/'imitation-1m-20261114/model-blocks/policy_weights.npz'
weights=np.load(previous);model=nn.Sequential(nn.Linear(128,128),nn.ReLU(),nn.Linear(128,64),nn.ReLU(),nn.Linear(64,1));model.load_state_dict({k:torch.from_numpy(weights[k]) for k in weights.files});model.eval()
ids=np.flatnonzero(splits==2);loss=top1=top3=0
with torch.no_grad():
 for ii in np.array_split(ids,int(np.ceil(len(ids)/256))):
  lens=lengths[ii];cols=np.concatenate([np.arange(n) for n in lens]);rows=np.repeat(np.arange(len(ii)),lens);indices=np.repeat(off[ii],lens)+cols
  z=torch.full((len(ii),int(lens.max())),-1e9);z[torch.as_tensor(rows),torch.as_tensor(cols)]=model(torch.from_numpy(x[indices])).flatten();target=torch.as_tensor(y[ii])
  loss+=nn.functional.cross_entropy(z,target,reduction='sum').item();top1+=(z.argmax(1)==target).sum().item();top3+=(z.topk(min(3,z.shape[1]),dim=1).indices==target[:,None]).any(1).sum().item()
report=dict(weights=str(previous),weights_sha256=hashlib.sha256(previous.read_bytes()).hexdigest(),test=dict(positions=len(ids),loss=loss/len(ids),top1=top1/len(ids),top3=top3/len(ids)),note='Same original-only test maps as new policy; descriptive comparison, not an arena-strength test.')
(ROOT/'previous-policy-test.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
