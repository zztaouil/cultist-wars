"""Verify disabled optimization against frozen baseline at fixed depth."""
import sys,json,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run
D=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as td:
 t=Path(td);p=t/'disabled-source.c';p.write_text('#define Q_LAZY_EVAL 0\n'+(D/'q-lazy-eval.c').read_text())
 libs=[build(D/'baseline.c',t,'baseline'),build(p,t,'disabled')]
 ps=json.loads((D.parent/'lmr/positions.json').read_text())+json.loads((D.parent/'lmr/tactics.json').read_text())+json.loads((D/'corpus.json').read_text())
 for fixture in ps:
  rs=[]
  for lib in libs:setup(lib,fixture['walls']);rs.append(run(lib,fixture['state'],5))
  assert all(rs[0][k]==rs[1][k] for k in ['value','action','nodes','qnodes','stopped']),fixture['name']
 print(json.dumps(dict(disabled_parity=len(ps),depth=5)))
