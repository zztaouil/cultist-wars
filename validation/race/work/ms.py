import json,sys,glob,math,collections as C
for tag in sys.argv[1:]:
  G=C.defaultdict(dict)
  for f in glob.glob(f'm_{tag}_?.jsonl'):
    for l in open(f):
      r=json.loads(l);G[r['map']][r['seatB']]=(r['B']>r['A'])-(r['B']<r['A'])
  g=[v for m in G.values() for v in m.values()];n=len(g);w=g.count(1);d=g.count(0);l=g.count(-1)
  pr=[(m[0]+m[1])/2 for m in G.values() if len(m)==2]  # paired score in [-1,1]
  mean=sum(pr)/len(pr);sd=(sum((x-mean)**2 for x in pr)/(len(pr)-1))**.5;se=sd/math.sqrt(len(pr))
  s0=[m[0] for m in G.values() if 0 in m];s1=[m[1] for m in G.values() if 1 in m]
  sc=lambda L:(sum(x==1 for x in L)+.5*sum(x==0 for x in L))/len(L)*100
  print(f"{tag}: B(candidate) {w}W/{d}D/{l}L of {n}  score {100*(w+d/2)/n:.1f}%  paired 95% CI {50+50*(mean-1.96*se):.1f}–{50+50*(mean+1.96*se):.1f}  as P0 {sc(s0):.1f}% as P1 {sc(s1):.1f}%  pairs {len(pr)}")
