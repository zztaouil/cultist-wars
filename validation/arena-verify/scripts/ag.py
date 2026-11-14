import json,sys,subprocess,ctypes as C,os
import harness as h
from pathlib import Path
spec,tag,part,nparts=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
src,*flags=spec.split(':')
p=Path('build');c=p/(tag+'.c');c.write_text(h.CODE.replace('SOURCE',str(Path(src).resolve())))
so=p/(tag+'.so');subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC','-DBUDGET_MS='+os.environ.get('BMS','49'),*[f'-D{f}' for f in flags if f],str(c),'-o',str(so)],check=True);lib=C.CDLL(str(so.resolve()))
R=[json.loads(l) for l in open(os.environ['HOME']+'/mnt/cultist-wars/validation/arena-study/results/agree.jsonl')]
out=open(f'ag_{tag}.jsonl','w')
R=[r for r in R if r['player']!='hexagonee'][:int(os.environ.get('NPOS','99999'))]
for r in R[part::nparts]:
  h.setup(lib,r['walls']);a=h.run(lib,r['state']);out.write(json.dumps(dict(player=r['player'],turn=r['turn'],theirs=r['theirs'],saved=r['ours'],ours=a['action'],depth=a['depth']))+'\n')
