from L import *
import statistics as S, math
ok=bad=to=0;seat=C.Counter();badl=[]
pl=C.defaultdict(lambda:C.defaultdict(list))
pairs=C.defaultdict(lambda:[0,0,0,0])  # unordered pair -> stats
rd=[]
dec=C.defaultdict(lambda:C.defaultdict(list))
phases=[(0,40),(40,100),(100,151)]
P=['aCat','Noobkins','MSz','Zylo','KawattaTaido','jacek','Pduhard-','putibuzu','tarriel','hexagonee','Lv0Sys','N3l']
lens=[]
for g in games():
  m=g['meta'];sc=m['scores'];hasTO='TIMEOUT' in m['actions']
  c=counts(g['states'][-1])
  if c==[int(sc[0]),int(sc[1])]: ok+=1
  else:
    bad+=1;badl.append((g['id'],sc,c,hasTO,len(m['actions'])))
  r0=(sc[0]>sc[1])-(sc[0]<sc[1]);seat[r0]+=1
  ag=sorted(m['agents'],key=lambda a:a['index'])
  rd.append(((ag[0].get('raw') or {}).get('score',float('nan'))-(ag[1].get('raw') or {}).get('score',float('nan')),r0,min(sc)<0,ag[0]['pseudo'],ag[1]['pseudo']))
  lens.append(len(g['states'])-1)
  for side in (0,1):
    name=CUR.get(ag[side]['agentId'])
    if name not in P: continue
    r=(sc[side]>sc[1-side])-(sc[side]<sc[1-side])
    pl[name]['res'].append(r);pl[name]['s%d'%side].append(r)
    St=g['states'];A=g['actions'];comp={(p,c):0 for p in range(3) for c in 'NEK'};cn=C.Counter()
    for k in range(len(St)-1):
      ph=0 if k<40 else 1 if k<100 else 2
      a,b=St[k],St[k+1];mover=a[56];sgn=1 if mover==side else -1
      for i in range(14):
        if a[4*i+1]<=0: continue
        po,pn,hn=a[4*i+2],b[4*i+2],b[4*i+1]
        if po!=pn: comp[(ph,'N' if po==2 else 'E')]+=sgn*(1 if po==2 else 2)
        elif hn<=0 and po!=2: comp[(ph,'K')]+=(1 if po!=side else -1)
      if mover==side:
        act=A[k];cn[['WAIT','MOVE','SHOOT','CONVERT'][act[0]]]+=1
        if act[0]==2 and a[4*act[2]+2]==2: cn['shootN']+=1
        if act[0]==1 and a[4*act[1]+3]: cn['moveL']+=1
    tag=name+'|'+'LDW'[r+1]
    for kk,v in comp.items(): dec[tag][kk].append(v)
    if name=='hexagonee' and ag[1-side]['pseudo']=='BOSS':
      for kk,v in comp.items(): dec['hexagonee|vsBOSS'][kk].append(v)
      pl['boss']['res'].append(r);pl['boss']['s%d'%side].append(r)
    for k in ['WAIT','MOVE','SHOOT','CONVERT','shootN','moveL']: pl[name][k].append(cn[k])
print('match',ok,'mismatch',bad,'mismatch with TIMEOUT marker or neg score',sum(1 for b in badl if b[3] or min(b[1])<0))
print([b for b in badl if not(b[3] or min(b[1])<0)][:5])
print('seat0',seat, 'P0 share decisive %.3f'%(seat[1]/(seat[1]+seat[-1])))
cl=[r for r in rd if not r[2]];s=C.Counter(r[1] for r in cl);print('excluding timeout games',s,'%.3f'%(s[1]/(s[1]+s[-1])))
sc=lambda L:(sum(x==1 for x in L)+.5*sum(x==0 for x in L))/max(1,len(L))*100
print('player N score s0 s1 n0 n1 | shoot move conv wait | shootN/g moveL/g')
for n in P+['boss']:
  d=pl[n]
  if n=='boss': print('vsBoss',len(d['res']),C.Counter(d['res']),'s0',C.Counter(d['s0']),'s1',C.Counter(d['s1']));continue
  tot=sum(sum(d[k]) for k in ['WAIT','MOVE','SHOOT','CONVERT'])
  print(f"{n:13s}{len(d['res']):4d} {sc(d['res']):5.1f} {sc(d['s0']):5.1f} {sc(d['s1']):5.1f} {len(d['s0']):4d} {len(d['s1']):4d} | "+' '.join(f"{sum(d[k])/tot*100:5.1f}" for k in ['SHOOT','MOVE','CONVERT','WAIT'])+f" | {S.mean(d['shootN']):.2f} {S.mean(d['moveL']):.2f}")
for t in sorted(dec):
  d=dec[t];n=len(d[(0,'N')])
  if n<8: continue
  def f(p,c):
    v=d[(p,c)];return f"{S.mean(v):+.2f}±{1.96*S.pstdev(v)/math.sqrt(n):.2f}"
  print(f"{t:18s}{n:4d} | "+' | '.join(' '.join(f(p,c) for c in 'NK') for p in range(3)))
json.dump(rd,open('rd.json','w'))
print('mean plies',S.mean(lens))
