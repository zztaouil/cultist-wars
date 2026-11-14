import sys,json,subprocess,ctypes as C
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1];sys.path.insert(0,str(ROOT/'validation/lmr'))
from run import build,setup,run,Array,Move
ps=json.loads((D/'positions.json').read_text());ds=[json.loads(l) for l in (D/'diagnosis.jsonl').read_text().splitlines()];gs={g['id']:g for g in json.loads((D/'games.json').read_text())}
lib=build(ROOT/'submissions/41709358.c',D,'branch',budget=49)
subprocess.run(['javac','-d',str(D/'java'),*map(str,(ROOT/'source/src/main/java/model').glob('*.java')),str(ROOT/'validation/strength/Replay.java')],check=True)
def cmd(a):
 k,i,t=a;return 'WAIT' if not k else f'{i} MOVE {t%13} {t//13}' if k==1 else f'{i} '+('SHOOT ' if k==2 else 'CONVERT ')+str(t)
import re
with (D/'branches.jsonl').open('w') as f:
 for d in ds:
  if d['gap'] is None or d['gap']<400:continue
  p=ps[str(d['game'])];s=p['states'][d['turn']];live=[]
  for _ in range(3):setup(lib,p['walls']);live.append(run(lib,s))
  branches=[]
  for a in [d['actual'],d['alternative']]:
   setup(lib,p['walls']);state=Array(*s);states=[];moves=[]
   for k in range(16):
    if lib.ended(state):break
    action=a if k==0 else run(lib,state,5)['action'];moves.append(cmd(action));lib.step(state,Move(*action,0));states.append(list(state))
   prefix=gs[d['game']]['actions'][:d['turn']]
   seed=re.search(r'seed=(-?\d+)',gs[d['game']]['ref'])[1]
   j=subprocess.run(['java','-cp',str(D/'java'),'model.Replay',seed],input='\n'.join(prefix+moves)+'\n',capture_output=True,text=True,check=True)
   js=[json.loads(l) for l in j.stdout.splitlines()][1:]
   for k,z in enumerate(states):assert js[d['turn']+k+1]['state']==z
   branches.append(dict(choice=a,moves=moves,counts=[sum(state[4*i+1]>0 and state[4*i+2]==side for i in range(14)) for side in (0,1)],verified=len(states)))
  result=dict(**d,repeats49=live,branches=branches);f.write(json.dumps(result)+'\n');f.flush();print(json.dumps(result),flush=True)
