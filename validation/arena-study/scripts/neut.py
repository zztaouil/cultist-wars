from load import *
import json,collections as C,statistics as S
H=json.load(open('data/handles.json'));AG={v[1]:k for k,v in H.items()}
P=['aCat','Noobkins','MSz','Zylo','jacek','KawattaTaido','hexagonee','Lv0Sys','N3l']
agg=C.defaultdict(lambda:C.defaultdict(list))
for g in games():
  ag=sorted(g['meta']['agents'],key=lambda a:a['index'])
  for side in (0,1):
    name=AG.get(ag[side]['agentId'])
    if name not in P: continue
    S_=g['states'];A=g['actions']
    c=C.Counter()
    for k in range(len(S_)-1):
      a,b=S_[k],S_[k+1]
      if a[56]!=side: continue
      act=A[k]
      if act[0]==2:
        tgt=act[2];own=a[4*tgt+2]
        c['shoot_'+('N' if own==2 else 'E' if own!=side else 'self')]+=1
        # actual victim
        for i in range(14):
          if a[4*i+1]>b[4*i+1]:
            vo=a[4*i+2];c['hit_'+('N' if vo==2 else 'E' if vo!=side else 'self')]+=1
            if b[4*i+1]<=0: c['kill_'+('N' if vo==2 else 'E' if vo!=side else 'self')]+=1
      if act[0]==1:
        u=act[1]
        c['move_'+('L' if a[4*u+3] else 'C')]+=1
      if act[0]==0:c['wait']+=1
      # leader alive ratio
    for k in ['shoot_N','shoot_E','shoot_self','hit_N','hit_E','hit_self','kill_N','kill_E','kill_self','move_L','move_C','wait']: agg[name][k].append(c[k])
ks=['shoot_N','hit_N','kill_N','shoot_E','hit_self','kill_self','move_L','move_C','wait']
print('player       '+' '.join(f'{k:>9s}' for k in ks))
for n in P: print(f'{n:12s} '+' '.join(f'{S.mean(agg[n][k]):9.2f}' for k in ks))
