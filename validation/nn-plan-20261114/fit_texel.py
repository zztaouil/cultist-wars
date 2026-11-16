"""E2: Texel fit of hand-eval term weights per regime. Unit weight fixed 1000."""
import numpy as np,torch,sys,json,hashlib
Z=np.load(sys.argv[1]);S,V,T,THR,RES,MAP=Z['S'],Z['V'],Z['T'],Z['THR'],Z['RES'],Z['MAP']
lam=float(sys.argv[2]) if len(sys.argv)>2 else 0.3
hp=S[:,1:56:4];own=S[:,2:56:4];led=S[:,3:56:4];alive=hp>0
neut=(alive&(own==2)).sum(1);leaders=(alive&(led==1)&(own<2)).sum(1)
reg=np.where(neut==0,2,np.where(leaders==2,0,1))
ok=(THR==0)&(np.abs(V)<900000)
val=np.array([int(hashlib.md5(str(m).encode()).hexdigest(),16)%7==0 for m in MAP])
W0=np.array([1000,18,1,1,1,1,1,1],np.float32)
def fitK(t,v,y):
  best=None
  for K in [100,150,200,300,400,500,600,800,1000,1300,1600,2000]:
    p=1/(1+np.exp(-(t@W0)/K));l=np.mean((p-y)**2)
    if best is None or l<best[0]:best=(l,K)
  return best[1]
out={}
for r in range(3):
  m=ok&(reg==r);t=T[m];v=V[m];y=(1-lam)*(RES[m]+1)/2+lam/(1+np.exp(-v/600.0));vm=val[m]
  K=fitK(t[~vm],v[~vm],y[~vm])
  tt=torch.tensor(t/np.array([1,10,100,100,100,100,100,100],np.float32));yy=torch.tensor(y,dtype=torch.float32)
  scale=torch.tensor([1,10,100,100,100,100,100,100],dtype=torch.float32)
  w=torch.tensor(W0*np.array([1,10,100,100,100,100,100,100]),requires_grad=True)
  opt=torch.optim.Adam([w],lr=0.5);tr=torch.tensor(~vm);te=torch.tensor(vm)
  def loss(mask):
    wf=torch.cat([torch.tensor([1000.]),w[1:]]);p=torch.sigmoid((tt[mask]*wf).sum(1)/K);return ((p-yy[mask])**2).mean()
  base_tr=loss(tr).item();base_te=loss(te).item()
  for it in range(3000):
    opt.zero_grad();l=loss(tr)+1e-9*(w[1:]**2).sum();l.backward();opt.step()
  wf=(torch.cat([torch.tensor([1000.]),w[1:]])/scale).detach().numpy()
  out[r]=dict(n=int(m.sum()),K=K,base_tr=base_tr,base_val=base_te,fit_tr=loss(tr).item(),fit_val=loss(te).item(),w=[round(float(x),3) for x in wf])
  print(r,json.dumps(out[r]))
json.dump(out,open(f'texel_lam{lam}.json','w'),indent=1)
