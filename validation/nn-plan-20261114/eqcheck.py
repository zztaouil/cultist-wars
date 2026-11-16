import json,random,lib,sys
sys.path.insert(0,str(lib.ROOT/'validation/arena-verify/scripts'));import harness as h
A=lib.build('submissions/41715067.cpp','eqA',5);B=lib.build(sys.argv[1],'eqB',5)
maps=json.load(open(lib.Path(__file__).parent/'maps_gate.json'));rng=random.Random(3);bad=n=0
for gid,w,s0 in maps[:8]:
  h.setup(A,w);h.setup(B,w);s=list(s0)
  for ply in range(100):
    acts=lib.actions(A,s);s=h.step(A,s,rng.choice(acts));c=[sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p) for p in (0,1)]
    if min(c)==0:break
    a=A.static_eval(lib.A62(*s));b=B.static_eval(lib.A62(*s));n+=1;bad+=a!=b
print('n',n,'diff',bad)
