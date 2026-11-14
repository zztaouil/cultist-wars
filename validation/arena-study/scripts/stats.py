import json,glob,re,collections as C
G=[json.load(open(f)) for f in glob.glob('data/games/*.json')]
P=['aCat','Noobkins','MSz','Zylo','KawattaTaido','jacek','Pduhard-','putibuzu','tarriel','hexagonee','Lv0Sys','N3l']
def acts(g):
  out=[]
  for f in g['frames']:
    if f['agent'] is None or f['agent']<0: continue
    s=f['stdout'].strip()
    if not s and 'timeout' not in f['summary'].lower() : continue
    out.append((f['agent'],s.split(';')[0].strip(),f['summary']))
  return out
rec=C.defaultdict(lambda:C.Counter()); h2h=C.defaultdict(C.Counter); sty=C.defaultdict(C.Counter); seatwin=C.Counter(); lens=C.defaultdict(list); margins=C.defaultdict(list); firstconv=C.defaultdict(list); waits=C.Counter()
for g in G:
  names=[a['pseudo'] for a in sorted(g['agents'],key=lambda a:a['index'])]
  sc=g['scores']
  if len(sc)!=2: continue
  res=[(sc[0]>sc[1])-(sc[0]<sc[1]),(sc[1]>sc[0])-(sc[1]<sc[0])]
  seatwin[res[0]]+=1
  A=acts(g)
  for i,n in enumerate(names):
    o=names[1-i]
    rec[n]['WDL'[1-res[i]] if res[i]>=0 else 'L']+=1
    rec[n][['L','D','W'][res[i]+1]]+=0
    h2h[n][(o,['L','D','W'][res[i]+1])]+=1
    margins[n].append(sc[i]-sc[1-i])
    mine=[a for a in A if a[0]==i]
    lens[n].append(len(A))
    fc=next((k for k,a in enumerate(mine) if 'CONVERT' in a[1]),None)
    if fc is not None: firstconv[n].append(fc)
    for a in mine:
      w=a[1].split()
      t='WAIT' if (not w or w[0]=='WAIT') else (w[1] if len(w)>1 else 'OTHER')
      sty[n][t]+=1
      if 'timeout' in a[2].lower(): sty[n]['TIMEOUT']+=1
# fix rec
R={}
for n in P:
  W=sum(v for (o,r),v in h2h[n].items() if r=='W');D=sum(v for (o,r),v in h2h[n].items() if r=='D');L=sum(v for (o,r),v in h2h[n].items() if r=='L')
  t=sty[n];tot=sum(v for k,v in t.items() if k!='TIMEOUT')
  print(f"{n:13s} {W:4d}W {D:4d}D {L:4d}L  score%={(W+D/2)/max(1,W+D+L)*100:5.1f}  draw%={D/max(1,W+D+L)*100:4.1f}  "+' '.join(f"{k}={t[k]/tot*100:4.1f}" for k in ['MOVE','SHOOT','CONVERT','WAIT'])+f" TO={t['TIMEOUT']} len={sum(lens[n])/len(lens[n]):5.1f} margin={sum(margins[n])/len(margins[n]):+.2f} 1stconv={sum(firstconv[n])/max(1,len(firstconv[n])):.1f}")
print('seat0 result',seatwin)
json.dump({n:{f'{o}|{r}':v for (o,r),v in h2h[n].items()} for n in P},open('data/h2h.json','w'))
