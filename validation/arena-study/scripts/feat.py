from load import *
import json,collections as C,statistics as S
H=json.load(open('data/handles.json'));AG={v[1]:k for k,v in H.items()}
P=['aCat','Noobkins','MSz','Zylo','KawattaTaido','jacek','Pduhard-','putibuzu','tarriel','hexagonee','Lv0Sys','N3l']
st=C.defaultdict(lambda:C.defaultdict(list));opp=C.defaultdict(C.Counter);seat=C.defaultdict(C.Counter)
rows=[]
for g in games():
  ag=sorted(g['meta']['agents'],key=lambda a:a['index'])
  sc=g['meta']['scores']
  for side in (0,1):
    name=AG.get(ag[side]['agentId'])
    if name not in P: continue
    o=ag[1-side]['pseudo']
    r=(sc[side]>sc[1-side])-(sc[side]<sc[1-side])
    S_=g['states'];A=g['actions']
    nconv=econv=kills=lost_shot=lost_conv=selfhit=0;dif={}
    for k in range(len(S_)-1):
      a,b=S_[k],S_[k+1];mover=a[56]
      for i in range(14):
        po,pn=a[4*i+2],b[4*i+2];ho,hn=a[4*i+1],b[4*i+1]
        if ho<=0: continue
        if mover==side:
          if po==2 and pn==side: nconv+=1
          if po==1-side and pn==side: econv+=1
          if po==1-side and hn<=0: kills+=1
          if po==side and hn<=0 and A[k][0]==2: selfhit+=1
        else:
          if po==side and pn==1-side: lost_conv+=1
          if po==side and hn<=0: lost_shot+=1
      if k in (20,40,60,90,120): c=counts(b);dif[k]=c[side]-c[1-side]
    d=st[name]
    for key,v in dict(res=r,nconv=nconv,econv=econv,kills=kills,lost_shot=lost_shot,lost_conv=lost_conv,selfhit=selfhit,**{f'd{k}':v for k,v in dif.items()}).items(): d[key].append(v)
    d['seat'+str(side)].append(r)
    opp[name][(o,r)]+=1
    rows.append(dict(game=g['id'],name=name,side=side,opp=o,res=r,score=sc,dif=dif,nconv=nconv,econv=econv,kills=kills,lost_shot=lost_shot,lost_conv=lost_conv,n=len(S_)))
json.dump(rows,open('data/rows.json','w'))
m=lambda x:S.mean(x) if x else float('nan')
print('player        N  score% seat0% seat1%  nconv econv kills lostShot lostConv d20  d40  d60  d90  d120')
for n in P:
  d=st[n];N=len(d['res']);sc=lambda L:(sum(1 for x in L if x==1)+0.5*sum(1 for x in L if x==0))/max(1,len(L))*100
  print(f"{n:12s}{N:4d} {sc(d['res']):6.1f} {sc(d['seat0']):6.1f} {sc(d['seat1']):6.1f}  "+' '.join(f"{m(d[k]):5.2f}" for k in ['nconv','econv','kills','lost_shot','lost_conv'])+'   '+' '.join(f"{m(d[k]):+.2f}" for k in ['d20','d40','d60','d90','d120']))
print();print('hexagonee by opp:',sorted(((o,r,v) for (o,r),v in opp['hexagonee'].items()),key=lambda x:(x[0],x[1])))
