import json,sys,subprocess,ctypes as C
import harness as h
from pathlib import Path
spec=sys.argv[1];tag=sys.argv[2]
src,flags=spec.split(':') if ':' in spec else (spec,'')
p=Path('build');c=p/(tag+'.c');c.write_text(h.CODE.replace('SOURCE',str(Path(src).resolve())))
so=p/(tag+'.so');subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC','-DBUDGET_MS=49',*([f'-D{flags}'] if flags else []),str(c),'-o',str(so)],check=True);lib=C.CDLL(str(so.resolve()))
R=[json.loads(l) for l in open('data/agree.jsonl') if '"hexagonee"' not in l]
n=s=0
for r in R[:int(sys.argv[3])]:
  h.setup(lib,r['walls']);a=h.run(lib,r['state'])['action'];n+=1;s+=a==r['theirs']
print(spec,'agreement %.1f%% of %d'%(100*s/n,n))
