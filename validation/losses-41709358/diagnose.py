"""Matched-horizon counterfactuals using frozen submitted engine; not proof of wins."""
import sys,json,ctypes as C
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1];sys.path.insert(0,str(ROOT/'validation/lmr'))
from run import build,setup,run,Array,Move
ps=json.loads((D/'positions.json').read_text());games={g['id']:g for g in json.loads((D/'games.json').read_text())}
lib=build(ROOT/'submissions/41709358.c',D,'submitted',budget=49)
selection={921208877:[16,18,20,40,42,48,50,52,54],921210462:[92,110,114,116,118,120],921211508:[29,31,35,37,43,45,47],921210319:[29,31,51,53,55,57,59,85,87],921207904:[23,25,27,59,61,63,65],921211685:[39,41,43,45,47],921210755:[95,97,99,101,103],921209667:[35,37,67,69,71,75,77],921211936:[31,33,35,45,47],921210601:[25,27,49,51,53],921208414:[90,92,94,98,100,102]}
def cmd(a):
 k,i,t=a;return 'WAIT' if not k else f'{i} MOVE {t%13} {t//13}' if k==1 else f'{i} '+('SHOOT ' if k==2 else 'CONVERT ')+str(t)
with (D/'diagnosis.jsonl').open('w') as f:
 for gid,turns in selection.items():
  p=ps[str(gid)]
  for t in turns:
   s=p['states'][t];actual=p['actions'][t];setup(lib,p['walls']);ref=run(lib,s,6)
   if ref['stopped']:continue
   scores=[]
   for a in [actual,ref['action']]:
    child=Array(*s);lib.step(child,Move(*a,0));setup(lib,p['walls']);res=run(lib,child,5)
    scores.append(-res['value'] if not res['stopped'] else None)
   r=dict(game=gid,turn=t,actual=actual,alternative=ref['action'],actual_cmd=games[gid]['actions'][t],alternative_cmd=cmd(ref['action']),scores=scores,gap=scores[1]-scores[0] if None not in scores else None)
   f.write(json.dumps(r)+'\n');f.flush()
   if r['gap'] and r['gap']>100:print(json.dumps(r),flush=True)
