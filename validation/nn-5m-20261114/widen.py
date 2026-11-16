"""Net2Wider-style initialization; verify function before symmetry-breaking noise."""
import hashlib,io,json
from pathlib import Path
import torch
from torch import nn
R=Path(__file__).resolve().parent
torch.set_num_threads(3)
torch.manual_seed(71)
parent=R/'wide-parent.pt'
if not parent.exists():parent=R/'fast/best.pt'
source=parent.read_bytes()
(R/'wide-parent.pt').write_bytes(source)
c=torch.load(io.BytesIO(source),map_location='cpu',weights_only=False)
assert c['architecture']==[128,128,64,1]
w=c['state_dict'];i0=torch.cat([torch.arange(128),torch.randperm(128)[:64]]);i1=torch.cat([torch.arange(64),torch.randperm(64)[:32]])
n0=torch.bincount(i0,minlength=128);n1=torch.bincount(i1,minlength=64)
v={'0.weight':w['0.weight'][i0].clone(),'0.bias':w['0.bias'][i0].clone(),
   '2.weight':w['2.weight'][i1][:,i0]/n0[i0], '2.bias':w['2.bias'][i1].clone(),
   '4.weight':w['4.weight'][:,i1]/n1[i1], '4.bias':w['4.bias'].clone()}
def model(a,state):
 m=nn.Sequential(nn.Linear(128,a[0]),nn.ReLU(),nn.Linear(a[0],a[1]),nn.ReLU(),nn.Linear(a[1],1));m.load_state_dict(state);return m
x=torch.randn(1000,128)
with torch.no_grad():err=(model([128,64],w)(x)-model([192,96],v)(x)).abs().max().item()
assert err<.0002,err
for k in v:
 if k.endswith('weight'):v[k]+=torch.randn_like(v[k])*.0001
c.update(state_dict=v,architecture=[128,192,96,1]);torch.save(c,R/'wide-init.pt')
(R/'widen.json').write_text(json.dumps({'source':str(parent.relative_to(R)),'source_sha256':hashlib.sha256(source).hexdigest(),'architecture':c['architecture'],'max_initial_function_error':err,'weight_noise_std':.0001,'seed':71},indent=2))
print('widening max error',err,flush=True)
