"""Verify alternative branches with Java, not merely the shared C simulator."""
import sys,json,tempfile,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run,Array,Move
D=Path(__file__).resolve().parent;ROOT=D.parents[1]
ps=json.loads((D/'replay-positions.json').read_text());labels=[json.loads(l) for l in (D/'labels.jsonl').read_text().splitlines()]
def command(a):
 k,i,t=a
 return 'WAIT' if k==0 else f'{i} MOVE {t%13} {t//13}' if k==1 else f'{i} '+('SHOOT' if k==2 else 'CONVERT')+f' {t}'
with tempfile.TemporaryDirectory() as td:
 t=Path(td);subprocess.run(['javac','-d',td,*map(str,(ROOT/'source/src/main/java/model').glob('*.java')),str(D/'Replay.java')],check=True);lib=build(D/'baseline.c',t,'bot')
 for label in labels:
  if label['name'] not in ['boss-baseline-2000-turn32','budget35-boss-loss-turn18','budget35-boss-loss-turn60','budget35-lv0sys-turn28']:continue
  p=next(p for p in ps if p['name']==label['name']);prefix=[command(x['action']) for x in ps if x['game']==p['game'] and x['turn']<p['turn']]
  best=max(label['scores'],key=lambda x:x['value'])['action']
  for choice in [p['action'],best]:
   setup(lib,p['walls']);s=Array(*p['state']);moves=[];states=[]
   for turn in range(16):
    if lib.ended(s):break
    a=choice if turn==0 else run(lib,s,5)['action'];moves.append(command(a));lib.step(s,Move(*a,0));states.append(list(s))
   r=subprocess.run(['java','-cp',td,'model.Replay',str(p['seed'])],input='\n'.join(prefix+moves)+'\n',text=True,capture_output=True,check=True)
   rows=[json.loads(x) for x in r.stdout.splitlines()][1:]
   for i,expected in enumerate(states):assert rows[p['turn']+i+1]['state']==expected,(p['name'],i)
   counts=[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in (0,1)]
   print(json.dumps(dict(name=p['name'],choice=choice,moves=moves,counts=counts,java_verified=len(states))),flush=True)
