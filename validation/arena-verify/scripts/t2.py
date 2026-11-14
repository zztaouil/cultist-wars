import numpy as np,random
from sklearn.linear_model import LogisticRegression as LR
from sklearn.metrics import log_loss
A=np.load('X.npy')
c=dict(g=0,k=1,side=2,y=3,cnt=4,hp=5,lead=6,race=7,nl=8,cntL=9,hpL=10,ev=11,rd=12,late=13,nneu=14,wr=15,hpr=16,near=17,chp=18,lhp=19,tot=20)
dec=A[:,3]!=0.5;D=A[dec];y=D[:,3];g=D[:,0];k=D[:,1]
print('decisive positions',len(D),'games',len(set(g)))
def split(seed):
  ids=sorted(set(g));random.Random(seed).shuffle(ids);t=set(ids[:len(ids)//4]);return np.array([x in t for x in g])
def col(*names):
  out=[]
  for n in names:
    if n=='ev': out.append(D[:,c['ev']]/1000)
    elif n=='seat': out.append(1-2*D[:,c['side']])   # +1 if mover is P0
    elif n=='rd': out.append(np.nan_to_num(D[:,c['rd']]))
    elif n=='evL': out.append(D[:,c['ev']]/1000*D[:,c['late']])
    elif n=='raceL': out.append(D[:,c['race']]*D[:,c['late']])
    else: out.append(D[:,c[n]])
  return np.column_stack(out)
def ll(X,te,sel=None):
  tr=~te if sel is None else (~te)&sel; tt=te if sel is None else te&sel
  m=LR(C=1e3,max_iter=3000).fit(X[tr],y[tr]);return log_loss(y[tt],m.predict_proba(X[tt])[:,1]),m
sets={'ourEval':['ev'],'count':['cnt'],'cnt+hp+lead':['cnt','hp','lead'],'+race':['cnt','hp','lead','race'],'7feat':['cnt','hp','lead','race','nl','cntL','hpL'],
 '7feat+ev':['cnt','hp','lead','race','nl','cntL','hpL','ev'],
 'ourEval+phase':['ev','evL'],'ourEval+phase+seat':['ev','evL','seat'],'7feat+seat':['cnt','hp','lead','race','nl','cntL','hpL','seat'],
 'ourEval+race':['ev','race'],'ourEval+phase+race':['ev','evL','race'],'ourEval+phase+race+raceL':['ev','evL','race','raceL'],
 'seat only':['seat'],'rating only':['rd'],'rating+seat':['rd','seat'],
 'ourEval+phase+rating':['ev','evL','rd'],'ourEval+phase+race+rating':['ev','evL','race','rd'],
 '7feat+rating':['cnt','hp','lead','race','nl','cntL','hpL','rd'],
 '7feat no race':['cnt','hp','lead','nl','cntL','hpL'],
 '7feat race->wr':['cnt','hp','lead','wr','nl','cntL','hpL'],'7feat race->hpr':['cnt','hp','lead','hpr','nl','cntL','hpL'],'7feat +near':['cnt','hp','lead','race','near','nl','cntL','hpL'],
 '7feat split hp':['cnt','chp','lhp','lead','race','nl','cntL','hpL'],
}
res={n:[] for n in sets}
for seed in range(8):
  te=split(seed)
  for n,f in sets.items(): res[n].append(ll(col(*f),te)[0])
for n in sets: print(f"{n:28s} {np.mean(res[n]):.4f} ± {np.std(res[n]):.4f}   (seed3-like single: {res[n][3]:.4f})")
te=split(3)
print('coefs 7feat',np.round(ll(col(*sets['7feat']),te)[1].coef_[0],3))
print('coefs ev+phase+race',np.round(ll(col('ev','evL','race'),te)[1].coef_[0],3))
print('coefs 7feat+rating',np.round(ll(col(*sets['7feat+rating']),te)[1].coef_[0],3))
for lo,hi in [(0,40),(40,100),(100,151)]:
  sel=(k>=lo)&(k<hi);out=[]
  for n in ['ourEval','ourEval+race','cnt+hp+lead','+race','7feat']:
    out.append(f"{n}={np.mean([ll(col(*sets[n]),split(s),sel)[0] for s in range(4)]):.4f}")
  print(lo,hi,' '.join(out))
# subset: games involving hexagonee current agent? use rating file not available -> by game ids
