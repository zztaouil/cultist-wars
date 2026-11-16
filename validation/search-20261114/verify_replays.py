import json,sys,tempfile
from pathlib import Path
D=Path(__file__).resolve().parent
sys.path.insert(0,str(D.parent/'lmr'));from run import build,setup,Array,Move,run
rows=[];total=0
with tempfile.TemporaryDirectory() as td:
 lib=build(D/'candidate.c',Path(td),'verify')
 for g in json.loads((D/'reconstructed.json').read_text()):
  setup(lib,g['walls'])
  for i,(before,after) in enumerate(zip(g['states'],g['states'][1:])):
   s=Array(*before);lib.step(s,Move(*g['acts'][i],0));assert list(s)==after,(g['id'],i)
   total+=1
  for i in range(10,100,10):
   rows.append(dict(name=g['id']+'-'+str(i),walls=g['walls'],state=g['states'][i]))
(D/'latest-positions.json').write_text(json.dumps(rows))
print('Java/C exact transitions:',total)
