import numpy as np,torch,json,sys,lib,ctypes as C
sys.argv=[sys.argv[0]]+sys.argv[1:]
src,pt,tex=sys.argv[1],sys.argv[2],sys.argv[3]
L=lib.build('validation/nn-plan-20261114/'+src,'nnpar',5);L0=lib.build('validation/nn-plan-20261114/'+src,'nnpar0',5,['VN_ENABLE=0'])
import harness as h
Z=np.load(lib.Path(__file__).parent/'dall2.npz');idx=np.random.RandomState(0).choice(len(Z['S']),300,replace=False)
maps={int(m[0]):m[1] for m in json.load(open(lib.Path(__file__).parent/'maps_train.json'))}
M=torch.load(pt);sd=M['sd']
def corr(s):
  hv=np.zeros(M['H1'],np.float32);hv+=sd['sc.weight'].numpy()[:,0]*s[56]+sd['sc.weight'].numpy()[:,1]*s[57]/150
  for i in range(14):
    hp,own,led,pos=s[4*i+1],s[4*i+2],s[4*i+3],s[4*i]
    if hp<=0:continue
    c=4 if own==2 else own*2+(1-led);r=c*91+pos;hv+=sd['E.weight'][r].numpy()+hp/10*sd['F.weight'][r].numpy()
  hv=np.clip(hv,0,1);o=np.clip(sd['l2.weight'].numpy()@hv+sd['l2.bias'].numpy(),0,1);out=sd['l3.weight'].numpy()[0]@o+sd['l3.bias'].numpy()[0]
  return M['CMAX']*np.tanh(out)
err=[]
for k in idx:
  s=Z['S'][k].tolist();w=maps[int(Z['MAP'][k])];h.setup(L,w);h.setup(L0,w)
  a=L.static_eval(lib.A62(*s));b=L0.static_eval(lib.A62(*s))
  if abs(a)>900000:continue
  c=(a-b)*(1 if s[56]==0 else -1);err.append(c-corr(s))
err=np.abs(err);print('n',len(err),'max err',err.max(),'mean',err.mean())
