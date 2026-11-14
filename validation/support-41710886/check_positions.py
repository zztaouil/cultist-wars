import json,sys
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1];sys.path.insert(0,str(ROOT/'validation/lmr'))
from run import build,setup,run,Array,Move
P=json.loads((ROOT/'validation/losses-41709358/positions.json').read_text());rows=[json.loads(l) for l in (ROOT/'validation/losses-41709358/diagnosis.jsonl').read_text().splitlines()]
libs=[build(ROOT/'submissions/41709358.c',D,'diag_base'),build(D/'recruit-q.c',D,'diag_new')]
with (D/'position-check.jsonl').open('w') as f:
 for row in rows:
  p=P[str(row['game'])];s=p['states'][row['turn']];rr=[]
  for lib in libs:setup(lib,p['walls']);rr.append(run(lib,s))
  f.write(json.dumps(dict(game=row['game'],turn=row['turn'],baseline=rr[0],candidate=rr[1]))+'\n')
print('positions',len(rows))
