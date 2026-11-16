import json,random,sys,lib,statistics as st
import harness as h
A=lib.build(sys.argv[1],'spA',int(sys.argv[3]));B=lib.build(sys.argv[2],'spB',int(sys.argv[3]))
maps=json.load(open(lib.Path(__file__).parent/'maps_gate.json'));rng=random.Random(5);R={0:[],1:[]}
for gid,w,s0 in maps[:12]:
  for L in (A,B):h.setup(L,w)
  s=list(s0)
  for ply in range(40):
    acts=lib.actions(A,s);s=h.step(A,s,rng.choice(acts))
    if min(sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p) for p in (0,1))==0:break
    if ply%8==7:
      for k,L in ((0,A),(1,B)):h.setup(L,w);r=h.run(L,s);R[k].append((r['nodes'],r['depth']))
for k in (0,1):print(k,'median nodes',st.median(x[0] for x in R[k]),'median depth',st.median(x[1] for x in R[k]),len(R[k]))
