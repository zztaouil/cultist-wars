import json,random,numpy as np,collections as C
from load import *
import harness as h
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import log_loss
lib=h.build('bot.c','tex')
def bfs(walls,src):
  d=[99]*91;d[src]=0;q=[src]
  for p in q:
    x,y=p%13,p//13
    for nx,ny in((x+1,y),(x-1,y),(x,y+1),(x,y-1)):
      if 0<=nx<13 and 0<=ny<7:
        n=ny*13+nx
        if not walls[n] and d[n]==99:d[n]=d[p]+1;q.append(n)
  return d
X=[];y=[];grp=[];E=[];ph=[]
random.seed(3)
for g in games():
  sc=g['meta']['scores']
  if min(sc)<0: continue
  w=g['walls'];h.setup(lib,w);D={}
  for k in range(2,len(g['states'])-1,3):
    s=g['states'][k];side=s[56];o=1-side
    res=sc[side]>sc[o] and 1 or (0.5 if sc[side]==sc[o] else 0)
    if res==0.5: continue
    units=[(s[4*i],s[4*i+1],s[4*i+2],s[4*i+3]) for i in range(14)]
    alive=[u for u in units if u[1]>0]
    cnt=[sum(1 for u in alive if u[2]==p) for p in (0,1)]
    hp=[sum(u[1] for u in alive if u[2]==p) for p in (0,1)]
    L=[next((u for u in alive if u[2]==p and u[3]),None) for p in (0,1)]
    neutrals=[u for u in alive if u[2]==2]
    def dist(a,b):
      if a not in D:D[a]=bfs(w,a)
      return D[a][b]
    race=[0,0];raceN=0
    for n in neutrals:
      dd=[min((dist(L[p][0],nb) for nb in [n[0]-1,n[0]+1,n[0]-13,n[0]+13] if 0<=nb<91 and abs(nb%13-n[0]%13)+abs(nb//13-n[0]//13)==1 and not w[nb]),default=99) if L[p] else 99 for p in (0,1)]
      # side to move gets tempo: compare d_side <= d_opp
      if dd[side]<99 or dd[o]<99:
        if dd[side]<=dd[o]: race[0]+=1
        else: race[1]+=1
    rem=s[57]/150
    f=[cnt[side]-cnt[o],hp[side]-hp[o],(L[side] is not None)-(L[o] is not None),race[0]-race[1],len(neutrals)*((L[side] is not None)-(L[o] is not None)),(cnt[side]-cnt[o])*(1-rem),(hp[side]-hp[o])*(1-rem)]
    ev=lib.static_eval(h.Array(*s))
    X.append(f);E.append(ev);y.append(res);grp.append(g['id']);ph.append(k)
X=np.array(X,float);y=np.array(y);E=np.array(E,float).reshape(-1,1);grp=np.array(grp);ph=np.array(ph)
ids=sorted(set(grp));random.shuffle(ids);test=set(ids[:len(ids)//4]);te=np.array([g in test for g in grp])
names=['countDiff','hpDiff','leaderDiff','neutralRaceDiff','neutrals*leaderDiff','countDiff*late','hpDiff*late']
print('positions',len(y),'train',(~te).sum(),'test',te.sum())
base=LogisticRegression(C=1e3,max_iter=2000).fit(E[~te],y[~te]);print('our static eval  test logloss %.4f'%log_loss(y[te],base.predict_proba(E[te])[:,1]))
for cols in [[0],[0,1],[0,1,2],[0,1,2,3],[0,1,2,3,4],list(range(7))]:
  m=LogisticRegression(C=1e3,max_iter=2000).fit(X[~te][:,cols],y[~te]);p=m.predict_proba(X[te][:,cols])[:,1]
  print('features',[names[c] for c in cols],'test logloss %.4f'%log_loss(y[te],p),'coef',np.round(m.coef_[0],3))
m=LogisticRegression(C=1e3,max_iter=2000).fit(np.hstack([X,E/1000])[~te],y[~te]);print('all+ourEval %.4f'%log_loss(y[te],m.predict_proba(np.hstack([X,E/1000])[te])[:,1]),np.round(m.coef_[0],3))
# ratio hp:count
for lo,hi in [(0,40),(40,100),(100,151)]:
  sel=(ph>=lo)&(ph<hi)
  m=LogisticRegression(C=1e3,max_iter=2000).fit(X[sel&~te][:,[0,1,2,3]],y[sel&~te]);c=m.coef_[0]
  b=LogisticRegression(C=1e3,max_iter=2000).fit(E[sel&~te],y[sel&~te])
  print(f'phase {lo}-{hi}: coef count={c[0]:.3f} hp={c[1]:.3f} leader={c[2]:.3f} race={c[3]:.3f}  hp-per-unit={c[1]/c[0]:.3f}  leader/unit={c[2]/c[0]:.2f} race/unit={c[3]/c[0]:.2f}  fitted logloss {log_loss(y[sel&te],m.predict_proba(X[sel&te][:,[0,1,2,3]])[:,1]):.4f} ourEval {log_loss(y[sel&te],b.predict_proba(E[sel&te])[:,1]):.4f}')
