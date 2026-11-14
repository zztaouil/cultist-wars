import numpy as np,json,glob,random
from sklearn.linear_model import LogisticRegression as LR
from sklearn.metrics import log_loss,roc_auc_score
A=np.load('X.npy')
V={}
for d in (4,6):
  V[d]={}
  for f in glob.glob(f'sv_d{d}_*.jsonl'):
    for l in open(f):
      i,v,n=json.loads(l);V[d][i]=v
idx=np.array(sorted(V[4]));D=A[idx];y=D[:,3];g=D[:,0];k=D[:,1]
clip=lambda v:np.clip(np.array(v,float),-20000,20000)/1000
ev=D[:,11]/1000;v4=clip([V[4][i] for i in idx]);v6=clip([V[6][i] for i in idx]);late=D[:,13]
mate=np.abs(np.array([V[6][i] for i in idx]))>=100000
print('n',len(idx),'decided-by-search(d6 |v|>=1e5)',mate.mean())
F=dict(ev=ev,v4=v4,v6=v6,cnt=D[:,4],hp=D[:,5],lead=D[:,6],race=D[:,7],nl=D[:,8],cntL=D[:,9],hpL=D[:,10],near=D[:,17])
def X(names,ph=True):
  cols=[]
  for n in names:
    cols.append(F[n])
    if ph and n in('ev','v4','v6','race'): cols.append(F[n]*late)
  return np.column_stack(cols)
def cv(names,sel=None):
  r=[];a=[]
  for seed in range(6):
    ids=sorted(set(g));random.Random(seed).shuffle(ids);t=set(ids[:len(ids)//4]);te=np.array([x in t for x in g])
    tr=~te;tt=te
    if sel is not None: tr=tr&sel;tt=tt&sel
    m=LR(C=1e3,max_iter=3000).fit(X(names)[tr],y[tr]);p=m.predict_proba(X(names)[tt])[:,1]
    r.append(log_loss(y[tt],p));a.append(roc_auc_score(y[tt],p))
  return np.mean(r),np.std(r),np.mean(a)
S=[['ev'],['v4'],['v6'],['v6','race'],['v6','race','near'],['ev','race'],['cnt','hp','lead','race','nl','cntL','hpL'],['cnt','hp','lead','race','nl','cntL','hpL','v6'],['v6','hp'],['v6','lead'],['v6','hp','lead','race']]
for nm,sel in [('all',None),('0-40',k<40),('40-100',(k>=40)&(k<100)),('100+',k>=100)]:
  print('==',nm,int(sel.sum()) if sel is not None else len(y))
  for s in S:
    l,sd,a=cv(s,sel);print(f"  {'+'.join(s):40s} logloss {l:.4f}±{sd:.4f}  AUC {a:.4f}")
