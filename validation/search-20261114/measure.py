import json,sys,tempfile,statistics
from pathlib import Path
D=Path(__file__).resolve().parent
sys.path.insert(0,str(D.parent/'lmr'));from run import build,setup,run
ps=json.loads((D.parent/'strength/replay-positions.json').read_text())[::19]+json.loads((D.parent/'strength/corpus.json').read_text())
rows=[]
with tempfile.TemporaryDirectory() as td:
 t=Path(td);libs=[]
 for name in ['live','efficient']:
  p=t/(name+'-source.c');p.write_text((D/(name+'.c')).read_text().replace('static int use_futility = 1','static int use_futility = 0'))
  libs.append(build(p,t,name))
 for repeat in range(5):
  ns=[0,0];nodes=[0,0]
  for i,p in enumerate(ps):
   rs=[None,None]
   for k in ([0,1] if (repeat+i)%2 else [1,0]):
    setup(libs[k],p['walls']);rs[k]=run(libs[k],p['state'],6);ns[k]+=rs[k]['ns'];nodes[k]+=rs[k]['nodes']
   assert rs[0]['value']==rs[1]['value']
  rows.append(dict(repeat=repeat,ns=ns,nodes=nodes))
 result=dict(positions=len(ps),depth=6,rows=rows,median_ns=[statistics.median(r['ns'][i] for r in rows) for i in range(2)])
 (D/'performance.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
