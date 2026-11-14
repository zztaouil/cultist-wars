"""Predeclared development comparison followed by one untouched holdout."""
import subprocess,sys,json
from pathlib import Path
D=Path('validation/horizon-41711135')
def play(variant,phase,maps):
 tag='horizon_'+variant+'_'+phase;jobs=[]
 for part in range(2):
  jobs.append(subprocess.Popen([sys.executable,'tests/arena_match.py','--a',str(D/'baseline.c'),'--b',str(D/(variant+'.c')),'--tag',tag,'--out',str(D/'matches'),'--maps',str(D/(phase+'-maps.json')),'--maps-used',str(maps),'--budget-ms','49','--shuffle-seed','41711135','--parts','2','--part',str(part)]))
 assert all(p.wait()==0 for p in jobs)
 p=subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag',tag,'--out',str(D/'matches')],capture_output=True,text=True,check=True);r=json.loads(p.stdout);print(json.dumps(r),flush=True);return r
rows=[]
for variant in ('contest','extension','combined'):
 rows.append(dict(variant=variant,**play(variant,'development',8)));(D/'development-results.json').write_text(json.dumps(rows,indent=2))
# Resolve ties by simpler variant order; no tuning after observing holdout.
best=max(rows,key=lambda x:x['score'])['variant'];(D/'selected.json').write_text(json.dumps(dict(variant=best,rule='highest development score, ties contest > extension > combined')))
r=play(best,'holdout',16);(D/'holdout-results.json').write_text(json.dumps(dict(variant=best,**r),indent=2))
