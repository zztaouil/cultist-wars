from load import *
import json,collections as C
H=json.load(open('data/handles.json'));me=H['hexagonee'][1]
out=[]
for g in games():
  ag=sorted(g['meta']['agents'],key=lambda a:a['index'])
  sides=[i for i in (0,1) if ag[i]['agentId']==me]
  if not sides: continue
  side=sides[0];sc=g['meta']['scores'];r=(sc[side]>sc[1-side])-(sc[side]<sc[1-side])
  if r==1: continue
  S_=g['states'];A=g['actions']
  # timeline of material events
  ev=[];
  for k in range(len(S_)-1):
    a,b=S_[k],S_[k+1];mover=a[56]
    for i in range(14):
      if a[4*i+1]<=0:continue
      po,pn,ho,hn=a[4*i+2],b[4*i+2],a[4*i+1],b[4*i+1]
      if po!=pn: ev.append((k,'conv',('us' if mover==side else 'them'),'N' if po==2 else ('E' if po!=side else 'Ours'),i))
      elif hn<=0: ev.append((k,'kill',('us' if mover==side else 'them'), 'N' if po==2 else ('E' if po!=side else 'Ours'),i, 'byNeutral' if A[k][0]!=2 else ''))
  c=[counts(s) for s in S_];d=[x[side]-x[1-side] for x in c]
  lead_lost=next((k for k in range(len(d)) if d[k]<0 and all(v<0 for v in d[k:k+6])),None)
  out.append(dict(game=g['id'],opp=ag[1-side]['pseudo'],seat=side,res=r,final=sc,lead_lost=lead_lost,maxlead=max(d),d=d[::10],events=ev,leaders=[ (S_[-1][4*i+1]>0) for i in range(14) if S_[0][4*i+3]]))
json.dump(out,open('data/our_nonwins.json','w'))
for o in out:
  e=[x for x in o['events'] if x[2]=='them' and x[3]=='Ours' or (x[2]=='us' and x[3]=='Ours')]
  print(o['game'],o['opp'][:10].ljust(10),'seat',o['seat'],'res',o['res'],o['final'],'lostlead@',o['lead_lost'],'maxlead',o['maxlead'],'d:',o['d'],'leaders alive',o['leaders'])
  print('   our losses:',[(x[0],x[1],x[2]) for x in o['events'] if x[3]=='Ours'][:12])
