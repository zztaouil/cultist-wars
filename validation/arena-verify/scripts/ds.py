# build dataset: every 3rd ply of all validated games (incl. draws flagged), features + static eval + meta
import numpy as np,random
from L import *
import harness as h
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
rows=[];S=[];W=[]
gi=0
for g in games():
  m=g['meta'];sc=m['scores']
  if min(sc)<0 or 'TIMEOUT' in m['actions']: continue
  ag=sorted(m['agents'],key=lambda a:a['index'])
  rt=[(a.get('raw') or {}).get('score',np.nan) for a in ag]
  w=g['walls'];h.setup(lib,w);D={}
  def dist(a,b):
    if a not in D:D[a]=bfs(w,a)
    return D[a][b]
  gi+=1
  for k in range(2,len(g['states'])-1,3):
    s=g['states'][k];side=s[56];o=1-side
    res=1 if sc[side]>sc[o] else (0.5 if sc[side]==sc[o] else 0)
    units=[(s[4*i],s[4*i+1],s[4*i+2],s[4*i+3]) for i in range(14)]
    alive=[u for u in units if u[1]>0]
    cnt=[sum(1 for u in alive if u[2]==p) for p in (0,1)]
    hp=[sum(u[1] for u in alive if u[2]==p) for p in (0,1)]
    Ld=[next((u for u in alive if u[2]==p and u[3]),None) for p in (0,1)]
    neu=[u for u in alive if u[2]==2]
    race=[0,0];margin=0.0;wr=[0.0,0.0];near=[99,99];hpr=[0,0]
    dl=[]
    for n in neu:
      dd=[min((dist(Ld[p][0],nb) for nb in [n[0]-1,n[0]+1,n[0]-13,n[0]+13] if 0<=nb<91 and abs(nb%13-n[0]%13)+abs(nb//13-n[0]//13)==1 and not w[nb]),default=99) if Ld[p] else 99 for p in (0,1)]
      dl.append(dd)
      if dd[side]<99 or dd[o]<99:
        win=0 if dd[side]<=dd[o] else 1
        race[win]+=1;hpr[win]+=n[1]
        dw=dd[side] if win==0 else dd[o]
        wr[win]+=1.0/(1+dw)
      near[0]=min(near[0],dd[side]);near[1]=min(near[1],dd[o])
    # greedy sequential race: leaders alternately claim nearest unclaimed neutral they reach first (tempo to mover)
    lead=(Ld[side] is not None)-(Ld[o] is not None)
    # cultists (non-leader) counts and hp
    chp=[sum(u[1] for u in alive if u[2]==p and not u[3]) for p in (0,1)]
    lhp=[(Ld[p][1] if Ld[p] else 0) for p in (0,1)]
    ev=lib.static_eval(h.Array(*s))
    late=1-s[57]/150
    rows.append([gi,k,side,res,cnt[side]-cnt[o],hp[side]-hp[o],lead,race[0]-race[1],len(neu)*lead,(cnt[side]-cnt[o])*late,(hp[side]-hp[o])*late,
      ev,rt[side]-rt[o],late,len(neu),wr[0]-wr[1],hpr[0]-hpr[1],(min(near[1],30)-min(near[0],30)) if neu else 0,chp[side]-chp[o],lhp[side]-lhp[o],cnt[side]+cnt[o],int(g['id'])])
    S.append(s)
  W.append(w)
np.save('X.npy',np.array(rows,float));np.save('S.npy',np.array(S,np.int64));np.save('W.npy',np.array(W))
print(len(rows),gi)
