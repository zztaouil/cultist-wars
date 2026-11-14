"""The shipped bot.c must match the tested variant (bot_r.c:LEADERB=0) exactly."""
import json,sys
from pathlib import Path
R=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(R/'validation/arena-verify/scripts'))
import harness as h
A=h.build('bot.c','shipA')
import subprocess,ctypes as C
d=Path('build');c=d/'shipB.c';c.write_text(h.CODE.replace('SOURCE',str((R/'validation/arena-verify/scripts/bot_r.c').resolve())))
so=d/'shipB.so';subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC','-DBUDGET_MS=49','-DLEADERB=0',str(c),'-o',str(so)],check=True)
B=C.CDLL(str(so.resolve()))
pos=[json.loads(l) for l in open(R/'validation/arena-study/results/agree.jsonl')][:400]
bad=0;n=0
for depth in (4,6):
  for r in pos[::4]:
    h.setup(A,r['walls']);h.setup(B,r['walls'])
    a=h.run(A,r['state'],depth);b=h.run(B,r['state'],depth);n+=1
    if (a['value'],a['action'],a['nodes'])!=(b['value'],b['action'],b['nodes']):
      bad+=1
      if bad<4:print('MISMATCH',depth,a,b)
print(json.dumps(dict(compared=n,mismatches=bad)))
