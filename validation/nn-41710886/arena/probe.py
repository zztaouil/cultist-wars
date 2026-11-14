import json,sys,collections
from pathlib import Path
sys.path.insert(0,'validation/arena-verify/scripts');import harness as h
R=Path('validation/nn-41710886');libs=[h.build(str(R/'baseline.c'),'arena_nn_base'),h.build(str(R/'candidate.c'),'arena_nn_candidate')];rows=[]
for l in open(R/'arena/recon.txt'):
 if l.startswith('G '):_,gid,ws=l.split();w=list(map(int,ws));turn=0
 elif l.startswith('S '):
  s,a=json.loads('['+l[2:]+']')
  if gid=='921363527' and turn in [71,73,75,77,79,81]:
   runs=[]
   for lib in libs:
    rr=[]
    for _ in range(3):h.setup(lib,w);rr.append(h.run(lib,s))
    runs.append(rr)
   rows.append({'turn':turn,'arena_action':a,'baseline':runs[0],'nn':runs[1]})
  turn+=1
(R/'arena/probe.json').write_text(json.dumps(rows,indent=2))
for r in rows:print(r['turn'],'arena',r['arena_action'],*[ (k,[(x['action'],x['value'],x['depth']) for x in r[k]]) for k in ('baseline','nn')])
