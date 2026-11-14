import json,sys,random,subprocess,ctypes as C
import harness as h
from pathlib import Path
import numpy as np
from L import counts
A,B,part,nparts,nmaps,budget,tag=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5]),sys.argv[6],sys.argv[7]
def lib(spec,t):
  src,*flags=spec.split(':')
  p=Path('build');p.mkdir(exist_ok=True);c=p/(t+'.c');c.write_text(h.CODE.replace('SOURCE',str(Path(src).resolve())))
  so=p/(t+'.so');subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC',f'-DBUDGET_MS={budget}',*[f'-D{f}' for f in flags if f],str(c),'-o',str(so)],check=True);return C.CDLL(str(so.resolve()))
Lb=[lib(A,f'{tag}a{part}'),lib(B,f'{tag}b{part}')]
X=np.load('X.npy');S=np.load('S.npy');W=np.load('W.npy')
# initial states: reconstruct from recon first S line per game
maps=json.load(open('maps.json'))
random.Random(2024).shuffle(maps)
import os
fn=f'm_{tag}_{part}.jsonl'
done=set((json.loads(l)['map'],json.loads(l)['seatB']) for l in open(fn)) if os.path.exists(fn) else set()
out=open(fn,'a')
for gid,w,s0 in maps[:nmaps][part::nparts]:
  for seat in (0,1):
    if (gid,seat) in done: continue
    for l in Lb: h.setup(l,w)
    s=list(s0);n=0
    while True:
      c=counts(s)
      if c[0]==0 or c[1]==0 or s[57]<=0: break
      k=1 if s[56]==seat else 0
      r=h.run(Lb[k],s);s=h.step(Lb[0],s,r['action']);n+=1
    out.write(json.dumps(dict(map=gid,seatB=seat,B=c[seat],A=c[1-seat],plies=n))+'\n');out.flush()
