"""Self-play data: baseline at BUDGET ms, random opening plies, records every ply.
usage: gen.py WORKER NGAMES OUTDIR [budget]"""
import json,random,sys,os,lib
from pathlib import Path
sys.path.insert(0,str(lib.ROOT/'validation/arena-verify/scripts'));import harness as h
wk,ng,out=int(sys.argv[1]),int(sys.argv[2]),Path(sys.argv[3]);bud=int(sys.argv[4]) if len(sys.argv)>4 else 5;SRC=sys.argv[5] if len(sys.argv)>5 else 'submissions/41715067.cpp';TAG=sys.argv[6] if len(sys.argv)>6 else ''
out.mkdir(parents=True,exist_ok=True)
L=lib.build(SRC,f'gen{TAG}{bud}_{os.uname().machine}_{wk}',bud)
maps=json.load(open(Path(__file__).parent/'maps_train.json'))
fn=out/f'gen{TAG}_{os.uname().nodename}_{wk}.jsonl';done=sum(1 for _ in open(fn)) if fn.exists() else 0
rng=random.Random(hash((wk,done,os.uname().nodename,os.getpid())))
fh=open(fn,'a')
cnt=lambda s:[sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p) for p in (0,1)]
for g in range(done,ng):
    gid,w,s0=rng.choice(maps);h.setup(L,w);s=list(s0);rows=[];k=rng.randint(2,10);ply=0
    while True:
        c=cnt(s)
        if c[0]==0 or c[1]==0 or s[57]<=0:break
        if ply<k:
            A=[a for a in lib.actions(L,s) if a[0]!=0] or lib.actions(L,s);a=rng.choice(A)
        else:
            r=h.run(L,s);a=r['action'];v=r['value'];v=v if s[56]==0 else -v
            rows.append([s,v,r['depth'],lib.terms(L,s),lib.threat(L,s)])
        s=h.step(L,s,a);ply+=1
    d=c[0]-c[1];fh.write(json.dumps(dict(map=gid,res=(d>0)-(d<0),diff=d,rows=rows))+'\n');fh.flush()
