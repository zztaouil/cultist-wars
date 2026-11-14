import subprocess,sys
from pathlib import Path
R=Path('validation/nn-41710886');jobs=[]
for part in range(4):
 cmd=[sys.executable,'tests/arena_match.py','--a',str(R/'baseline.c'),'--b',str(R/'candidate.c'),'--tag','nn49','--out',str(R),'--maps',str(R/'match-maps.json'),'--maps-used','40','--budget-ms','49','--shuffle-seed','41710886','--parts','4','--part',str(part)]
 jobs.append(subprocess.Popen(cmd))
assert all(p.wait()==0 for p in jobs)
p=subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag','nn49','--out',str(R)],text=True,capture_output=True,check=True)
(R/'match-summary.json').write_text(p.stdout);print(p.stdout)
