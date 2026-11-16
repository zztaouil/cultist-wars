import json,re,collections,statistics
from pathlib import Path
D=Path(__file__).resolve().parent;G=json.loads((D/'reconstructed.json').read_text())
B=json.loads((D/'all-battles.json').read_text());C=collections.Counter()
for b in B:
 if b.get('done'):C['L' if b['my_position'] else 'D' if len({p['position'] for p in b['players']})==1 else 'W']+=1
print('overall',dict(C))
def count(s,p):return sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14))
rows=[]
for g in G:
 m=g['seat'];o=1-m;S=g['states'];r=dict(id=g['id'],opp=g['opp'],seat=m,result='L' if g['scores'][m]<g['scores'][o] else 'W' if g['scores'][m]>g['scores'][o] else 'D',score=[g['scores'][m],g['scores'][o]],verified=g['score_ok'])
 r['recruit']=[0,0];r['kills']=[0,0];r['enemy_convert']=[0,0];r['events']=[]
 for t,(s,z,a) in enumerate(zip(S,S[1:],g['acts'])):
  side=s[56];who=int(side!=m);k,u,tg=a
  if k==3:
   r['recruit' if s[4*tg+2]==2 else 'enemy_convert'][who]+=1;r['events'].append([t,'opp' if who else 'us','convert',tg,s[4*tg+2]])
  for i in range(14):
   if s[4*i+1]>0 and z[4*i+1]==0:
    if s[4*i+2]==1-side:r['kills'][who]+=1
    r['events'].append([t,'opp' if who else 'us','kill',i,s[4*i+2]])
 for t in [20,30,40,60,100,150]:
  s=S[min(t,len(S)-1)];r['t'+str(t)]={'count':[count(s,m),count(s,o)],'lhp':[s[4*m+1],s[4*o+1]],'neutrals':count(s,2)}
 r['depth_mid']=round(statistics.mean([x['depth'] for x in g['stats'][10:30]]),2) if g['stats'][10:30] else None
 r['rng_fail']=sum(x.get('rng')!=1 for x in g['stats'][2:]);r['elapsed_max']=max([x.get('elapsed_us',0) for x in g['stats']] or [0])
 rows.append(r)
for r in rows:
 print(r['id'],r['opp'],r['seat'],r['result'],r['score'],'rec',r['recruit'],'kills',r['kills'],'conv',r['enemy_convert'],'30',r['t30'],'60',r['t60'],'depth',r['depth_mid'],'ok',r['verified'])
print('bad',[(r['id'],r['score']) for r in rows if not r['verified']]);print('rng fails',sum(r['rng_fail'] for r in rows),'max us',max(r['elapsed_max'] for r in rows))
for res in ['L','W','D']:
 rs=[r for r in rows if r['result']==res and r['verified']];print(res,'n',len(rs),'seats',dict(collections.Counter(r['seat'] for r in rs)),'recruit deficit',sum(r['recruit'][0]<r['recruit'][1] for r in rs),'opp leader hp>=9 t30',sum(r['t30']['lhp'][1]>=9 for r in rs),'own leader dead60',sum(r['t60']['lhp'][0]==0 for r in rs))
(D/'rows.json').write_text(json.dumps(rows,indent=2))
