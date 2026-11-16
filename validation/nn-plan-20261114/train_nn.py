"""E3: sparse value-correction net on top of Texel eval.
usage: train_nn.py data.npz weights.json out_prefix [H1 H2 epochs]"""
import numpy as np,torch,json,sys,hashlib,math,time
torch.manual_seed(0);np.random.seed(0)
Z=np.load(sys.argv[1]);Wf=json.load(open(sys.argv[2]));out=sys.argv[3]
H1=int(sys.argv[4]) if len(sys.argv)>4 else 32;H2=int(sys.argv[5]) if len(sys.argv)>5 else 16;EP=int(sys.argv[6]) if len(sys.argv)>6 else 6
S,T,THR,RES,MAP,V=Z['S'],Z['T'],Z['THR'],Z['RES'],Z['MAP'],Z['V']
hp=S[:,1:56:4];own=S[:,2:56:4];led=S[:,3:56:4];pos=S[:,0:56:4];alive=hp>0
neut=(alive&(own==2)).sum(1);nl=(alive&(led==1)&(own<2)).sum(1);reg=np.where(neut==0,2,np.where(nl==2,0,1))
ok=(np.abs(V)<900000)
# base eval with deployed (clipped, alpha=1) weights
Wm=np.zeros((3,8),np.float32)
for r in range(3):
  w=np.array(Wf[str(r)]['w'],np.float32);Wm[r]=np.where(w>0,w,0)
base=(T*Wm[reg]).sum(1);K=np.array([Wf[str(r)]['K'] for r in range(3)],np.float32)[reg]
cat=np.where(own==2,4,own*2+(1-led))  # 0 own0 leader,1 own0 cultist,2 own1 leader,3 own1 cultist,4 neutral
idx=np.where(alive,cat*91+pos,455)
side=S[:,56];rem=S[:,57]
LAM=float(sys.argv[7]) if len(sys.argv)>7 else 0.0
Kr=np.array([Wf[str(r)]['K'] for r in range(3)],np.float32)[reg]
yres=(RES+1)/2.0
y=(1-LAM)*yres+LAM/(1+np.exp(-np.clip(V,-50000,50000)/Kr))
# walls: need map walls -> load maps
maps={int(m[0]):m[1] for m in json.load(open('maps_train.json'))}
wall=np.array([maps[m] for m in MAP],np.float32)
val=np.array([int(hashlib.md5(str(m).encode()).hexdigest(),16)%7==0 for m in MAP])
def aug_swap(idx,hpv,side,y,base):
  c=idx//91;p=idx%91;sw=np.array([2,3,0,1,4,5]);c2=np.where(idx==455,5,sw[np.minimum(c,5)]);i2=np.where(idx==455,455,c2*91+p)
  return i2,hpv,1-side,1-y,-base
keep=ok
YR=yres[keep];I=idx[keep];HPV=(hp[keep]/10.0).astype(np.float32);SD=side[keep];RM=(rem[keep]/150.0).astype(np.float32);Y=y[keep];B=base[keep];KK=K[keep];WL=wall[keep];VA=val[keep]
I2,_,SD2,Y2,B2=aug_swap(I,HPV,SD,Y,B)
I=np.concatenate([I,I2]);HPV=np.concatenate([HPV,HPV]);SD=np.concatenate([SD,SD2]);RM=np.concatenate([RM,RM]);Y=np.concatenate([Y,Y2]);YR=np.concatenate([YR,1-YR]);B=np.concatenate([B,B2]);KK=np.concatenate([KK,KK]);WL=np.concatenate([WL,WL]);VA=np.concatenate([VA,VA])
tr=np.where(~VA)[0];te=np.where(VA)[0]
print('train',len(tr),'val',len(te),flush=True)
CMAX=float(sys.argv[9]) if len(sys.argv)>9 else 500.0;USEW=float(sys.argv[8]) if len(sys.argv)>8 else 0.0
class Net(torch.nn.Module):
  def __init__(s):
    super().__init__();s.E=torch.nn.Embedding(456,H1,padding_idx=455);s.F=torch.nn.Embedding(456,H1,padding_idx=455)
    s.Wl=torch.nn.Linear(91,H1,bias=True);s.sc=torch.nn.Linear(2,H1,bias=False);s.l2=torch.nn.Linear(H1,H2);s.l3=torch.nn.Linear(H2,1)
    for e in (s.E,s.F):torch.nn.init.normal_(e.weight,0,0.05);e.weight.data[455]=0
    torch.nn.init.zeros_(s.l3.weight);torch.nn.init.zeros_(s.l3.bias)
  def forward(s,i,hv,wl,sc):
    h=s.E(i).sum(1)+(s.F(i)*hv.unsqueeze(2)).sum(1)+s.Wl(wl)*USEW+s.sc(sc)
    h=torch.clamp(h,0,1);h=torch.clamp(s.l2(h),0,1);return CMAX*torch.tanh(s.l3(h).squeeze(1))
net=Net();opt=torch.optim.AdamW(net.parameters(),lr=2e-3,weight_decay=1e-4)
tt=lambda a,dt=torch.float32:torch.tensor(a,dtype=dt)
TI=tt(I,torch.long);THV=tt(HPV);TW=tt(WL);TSC=tt(np.stack([SD,RM],1));TY=tt(Y);TYR=tt(YR);TB=tt(B);TK=tt(KK)
def lossf(ix,t=None):
  c=net(TI[ix],THV[ix],TW[ix],TSC[ix]);p=torch.sigmoid((TB[ix]+c)/TK[ix]);return ((p-(TY if t is None else t)[ix])**2).mean(),c
with torch.no_grad():
  p0=torch.sigmoid(TB[te]/TK[te]);base_val=((p0-TYR[te])**2).mean().item()
print('base val mse',base_val,flush=True)
best=(9,None);BS=4096
for ep in range(EP):
  perm=torch.tensor(np.random.permutation(tr));t0=time.time()
  for g in opt.param_groups:g['lr']=2e-3*(0.6**ep)
  for k in range(0,len(perm),BS):
    ix=perm[k:k+BS];opt.zero_grad();l,_=lossf(ix);l.backward();opt.step()
  with torch.no_grad():
    vl=0;cs=0
    for k in range(0,len(te),65536):
      l,c=lossf(torch.tensor(te[k:k+65536]),TYR);vl+=l.item()*len(c);cs+=c.abs().sum().item()
    vl/=len(te)
  print(ep,'val',round(vl,6),'base',round(base_val,6),'mean|c|',round(cs/len(te),1),round(time.time()-t0,1),'s',flush=True)
  if vl<best[0]:best=(vl,{k:v.clone() for k,v in net.state_dict().items()})
net.load_state_dict(best[1]);torch.save(dict(sd=best[1],H1=H1,H2=H2,CMAX=CMAX,base_val=base_val,val=best[0]),out+'.pt')
print('best val',best[0])
