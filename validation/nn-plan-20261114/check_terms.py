import json,random,lib,sys
sys.path.insert(0,str(lib.ROOT/'validation/arena-verify/scripts'));import harness as h
L=lib.build('submissions/41715067.cpp','gen5',5)
maps=json.load(open(lib.ROOT/'validation/arena-verify/results/maps.json'))
rng=random.Random(1);bad=0;n=0
for gid,w,s0 in maps[:5]:
    h.setup(L,w);s=list(s0)
    for ply in range(60):
        A=lib.actions(L,s);a=rng.choice(A)
        s=h.step(L,s,a)
        if sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==0)==0:break
        t=lib.terms(L,s);ev=L.static_eval(lib.A62(*s));pl0=ev if s[56]==0 else -ev
        tot=1000*t[0]+18*t[1]+sum(t[2:]);n+=1
        if abs(tot-pl0)>0.5 and abs(ev)<900000:bad+=1;print('mismatch',tot,pl0,t) if bad<4 else None
print('checked',n,'bad',bad)
