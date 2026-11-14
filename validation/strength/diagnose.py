import sys,json,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run,Array,Move
D=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as td:
 lib=build(D/'baseline.c',Path(td),'teacher',budget=500)
 for p in json.loads((D/'replay-positions.json').read_text()):
  if p['game'] not in ['boss-baseline-2000','boss-baseline-2001','budget35-boss-loss','budget35-lv0sys'] or p['turn']%2 or p['turn']>100:continue
  setup(lib,p['walls']);r=run(lib,p['state'])
  if r['action']==p['action']: regret=0;v=r['value']
  else:
   s=Array(*p['state']);lib.step(s,Move(*p['action'],0));setup(lib,p['walls']);rr=run(lib,s,max(1,min(r['depth']-1,7)));v=-rr['value'];regret=r['value']-v
  print(json.dumps(dict(name=p['name'],chosen=p['action'],teacher=r,chosen_reference=v,approx_gap=regret)),flush=True)
