import json,sys,random,subprocess
import harness as h, ctypes as C
from pathlib import Path
from load import *
A,B,part,np_=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
def lib(spec,tag):
  src,flags=spec.split(':') if ':' in spec else (spec,'')
  p=Path('build');c=p/(tag+'.c');c.write_text(h.CODE.replace('SOURCE',str(Path(src).resolve())))
  so=p/(tag+'.so');subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC','-DBUDGET_MS=49',*([f'-D{flags}'] if flags else []),str(c),'-o',str(so)],check=True);return C.CDLL(str(so.resolve()))
L=[lib(A,f'a{part}'),lib(B,f'b{part}')]
lib.__name__
end=L[0]
end.restype=C.c_int
maps=[]
random.seed(11)
allg=[g for g in games()]
random.shuffle(allg)
maps=[(g['id'],g['walls'],g['states'][0]) for g in allg[:int(sys.argv[5])]]
for gid,w,s0 in maps[part::np_]:
  for seat in (0,1):
    for l in L: h.setup(l,w)
    s=list(s0);n=0
    def ended(s):
      c=counts(s);return c[0]==0 or c[1]==0 or s[57]<=0
    while not ended(s):
      k=1 if s[56]==seat else 0   # seat = side played by B
      r=h.run(L[k],s);s=h.step(L[0],s,r['action']);n+=1
    c=counts(s);print(json.dumps(dict(map=gid,seatB=seat,B=c[seat],A=c[1-seat],plies=n)),flush=True)
