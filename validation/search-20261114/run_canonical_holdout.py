import subprocess,sys
from pathlib import Path
D=Path(__file__).resolve().parent
jobs=[]
for i in range(4):
 jobs.append(subprocess.Popen([sys.executable,'tests/arena_match.py','--a',str(D/'live.c'),'--b',str(D/'canonical.c'),'--tag','canonical49','--out',str(D),'--maps',str(D/'holdout-maps.json'),'--maps-used','64','--budget-ms','49','--part',str(i),'--parts','4']))
assert all(j.wait()==0 for j in jobs)
