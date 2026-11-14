"""Matched-depth action values, including actual saved actions, no root/child horizon mismatch."""
import sys,json,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run,Array,Move
D=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as td:
 t=Path(td);libs=[build(D/'baseline.c',t,'old'),build(D/'eval-hp.c',t,'new')]
 for p in json.loads((D/'corpus.json').read_text()):
  choices=[p['action']];searches=[]
  for k,depth in [(0,5),(1,5),(0,7)]:
   setup(libs[k],p['walls']);r=run(libs[k],p['state'],depth);searches.append(r);choices.append(r['action'])
  scored=[]
  for action in sorted(set(tuple(c) for c in choices)):
   child=Array(*p['state']);libs[0].step(child,Move(*action,0));setup(libs[0],p['walls']);r=run(libs[0],child,6)
   scored.append(dict(action=action,value=-r['value'],stopped=r['stopped']))
  print(json.dumps(dict(name=p['name'],saved=p['action'],searches=searches,scores=scored)),flush=True)
