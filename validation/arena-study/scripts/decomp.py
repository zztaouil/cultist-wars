from load import *
import json,collections as C,statistics as S
H=json.load(open('data/handles.json'));AG={v[1]:k for k,v in H.items()}
P=['aCat','Noobkins','MSz','Zylo','jacek','hexagonee','Lv0Sys','N3l']
phases=[(0,40),(40,100),(100,151)]
agg=C.defaultdict(lambda:C.defaultdict(list))
for g in games():
  ag=sorted(g['meta']['agents'],key=lambda a:a['index']);sc=g['meta']['scores']
  for side in (0,1):
    name=AG.get(ag[side]['agentId'])
    if name not in P: continue
    oppn=ag[1-side]['pseudo']
    r=(sc[side]>sc[1-side])-(sc[side]<sc[1-side])
    S_=g['states'];A=g['actions']
    comp={(p,c):0 for p in range(3) for c in 'NEK'}
    for k in range(len(S_)-1):
      ph=next(i for i,(a,b) in enumerate(phases) if a<=k<b)
      a,b=S_[k],S_[k+1];sgn=1 if a[56]==side else -1
      for i in range(14):
        if a[4*i+1]<=0:continue
        po,pn,hn=a[4*i+2],b[4*i+2],b[4*i+1]
        if po!=pn: comp[(ph,'N' if po==2 else 'E')]+= sgn*(1 if po==2 else 2)
        elif hn<=0 and po!=2: comp[(ph,'K')]+= (1 if po!=side else -1)
    tag=name+('|'+('W' if r==1 else 'D' if r==0 else 'L'))
    if name=='hexagonee' and oppn=='BOSS': tag='hexagonee|vsBOSS'
    for kk,v in comp.items(): agg[tag][kk].append(v)
    agg[tag]['n'].append(1)
print('tag                 n   | open0-40 N  E  K  | mid40-100 N  E  K | end100+ N  E  K')
for t in sorted(agg):
  d=agg[t];n=len(d['n'])
  print(f"{t:20s}{n:4d} | "+' | '.join(' '.join(f"{S.mean(d[(p,c)]):+.2f}" for c in 'NEK') for p in range(3)))
