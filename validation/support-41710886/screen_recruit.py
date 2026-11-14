import subprocess,sys
from pathlib import Path
D=Path('validation/support-41710886')
for weight in [1]:
 tag='recruitq'
 jobs=[]
 for part in range(4):
  cmd=[sys.executable,'tests/arena_match.py','--a','submissions/41709358.c','--b',f'{D}/recruit-q.c','--tag',tag,'--out',str(D),'--maps',str(D/'loss-maps.json'),'--maps-used','24','--budget-ms','20','--shuffle-seed','141126','--parts','4','--part',str(part)]
  jobs.append(subprocess.Popen(cmd))
 for p in jobs:assert p.wait()==0
 subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag',tag,'--out',str(D)],check=True)
