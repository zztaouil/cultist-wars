"""Alternate order; clock/counter-free fixed-depth ablation, three repetitions."""
import json,tempfile
from pathlib import Path
from run import D,build,setup,run
with tempfile.TemporaryDirectory() as td:
 td=Path(td);libs=[build(D/'baseline.c',td,'base',profile=0),build(D/'experiment.c',td,'off',profile=0),build(D/'experiment.c',td,'on',profile=0)]
 cases=json.loads((D/'positions.json').read_text())
 for repeat in range(3):
  for i,p in enumerate(cases):
   for k in ([0,1,2] if (repeat+i)%2 else [2,1,0]):
    setup(libs[k],p['walls']);r=run(libs[k],p['state'],6,k==2)
    r.pop('metrics',None)
    print(json.dumps(dict(repeat=repeat,position=p['name'],variant=k,**r)),flush=True)
