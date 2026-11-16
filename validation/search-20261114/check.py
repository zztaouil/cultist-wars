import sys,json,tempfile
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1]
sys.path.insert(0,str(ROOT/'validation/lmr'))
from run import build,setup,run
ps=json.loads((ROOT/'validation/strength/replay-positions.json').read_text())[::19]
ps+=json.loads((ROOT/'validation/strength/corpus.json').read_text())
rows=[]
with tempfile.TemporaryDirectory() as td:
 t=Path(td);libs=[]
 for name in ['live','candidate']:
  p=t/(name+'-source.c');p.write_text((D/(name+'.c')).read_text().replace('static int use_futility = 1','static int use_futility = 0'))
  libs.append(build(p,t,name))
 for i,p in enumerate(ps):
  rs=[]
  for lib in libs:
   setup(lib,p['walls']);rs.append(run(lib,p['state'],5))
  rows.append(dict(name=p['name'],results=rs))
  assert rs[0]['value']==rs[1]['value'],(p['name'],rs)
 (D/'parity.json').write_text(json.dumps(rows))
 print(json.dumps({'positions':len(rows),'ns':[sum(r['results'][i]['ns'] for r in rows) for i in range(2)],'nodes':[sum(r['results'][i]['nodes'] for r in rows) for i in range(2)]}))
