"""Single-factor comparisons, fresh caches, whole-map paired matches."""
import sys,json,tempfile,argparse,random
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run,fixture,Array,Move
D=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('task',choices=['fixed','tactical','matches']);ap.add_argument('--candidate',default='eval-hp.c');ap.add_argument('--opponent',default='baseline.c');ap.add_argument('--seed',type=int,default=3000);ap.add_argument('--pairs',type=int,default=8);ap.add_argument('--budget',type=int,default=49);a=ap.parse_args()
with tempfile.TemporaryDirectory() as td:
 t=Path(td);libs=[build(D/a.opponent,t,'old',budget=a.budget),build(D/a.candidate,t,'new',budget=a.budget)]
 if a.task=='matches':
  for seed in range(a.seed,a.seed+a.pairs):
   for seat in (seed%2,1-seed%2):
    s,w=fixture(seed)
    for lib in libs:setup(lib,w)
    stats=[[],[]]
    while not libs[0].ended(s):
     k=int(s[56]==seat);r=run(libs[k],s,observe=1);stats[k].append(r);libs[0].step(s,Move(*r['action'],0))
    counts=[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in (0,1)]
    print(json.dumps(dict(seed=seed,seat=seat,new=counts[seat],baseline=counts[1-seat],stats=stats)),flush=True)
 else:
  ps=json.loads((D.parent/'lmr/positions.json').read_text())+json.loads((D.parent/'lmr/tactics.json').read_text())
  if (D/'corpus.json').exists():ps+=json.loads((D/'corpus.json').read_text())
  for repeat in range(3 if a.task=='fixed' else 1):
   for p in ps:
    rs=[None,None]
    for k in (repeat%2,1-repeat%2):
     setup(libs[k],p['walls']);rs[k]=run(libs[k],p['state'],6 if a.task=='fixed' else 5)
     if a.task=='tactical':
      child=Array(*p['state']);libs[0].step(child,Move(*rs[k]['action'],0));setup(libs[0],p['walls']);rs[k]['teacher_value']=-run(libs[0],child,6)['value']
    print(json.dumps(dict(name=p['name'],repeat=repeat,results=rs)),flush=True)
