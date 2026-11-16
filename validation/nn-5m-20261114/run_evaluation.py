"""Export/check both models; select by quantized validation loss; paired matches."""
import json,shutil,subprocess,sys
from pathlib import Path
from match_guard import guard
R=Path(__file__).resolve().parent;root=R.parents[1]
def run(name):subprocess.run([sys.executable,str(R/name)],cwd=root,check=True)
for name in ['export.py','check.py','export_wide.py','check_wide.py','verify_disabled.py','evaluate_previous.py']:run(name)
losses={n:json.loads((R/f).read_text())['validation']['loss'] for n,f in [('candidate','parity.json'),('wide','wide-parity.json')]}
selected=min(losses,key=losses.get);shutil.copyfile(R/f'{selected}.c',R/'selected.c')
(R/'selection.json').write_text(json.dumps(dict(selected=selected,quantized_validation_losses=losses,criterion='Minimum quantized validation cross-entropy; matches and test metrics not used.'),indent=2))
print('Selected',selected,losses,flush=True)
for name in ['tree_variant.py','verify_tree.py','check_runtime.py','local_matches.py']:run(name)
for tag,a,b in [('wide49','baseline','wide'),('tree49','selected','tree')]:
 guard(tag,R/f'{a}.c',R/f'{b}.c')
 jobs=[]
 for part in range(4):
  cmd=[sys.executable,'tests/arena_match.py','--a',str(R/f'{a}.c'),'--b',str(R/f'{b}.c'),'--tag',tag,'--out',str(R),'--maps',str(R/'match-maps.json'),'--maps-used','64','--budget-ms','49','--shuffle-seed','41723000','--parts','4','--part',str(part)]
  jobs.append(subprocess.Popen(cmd,cwd=root))
 assert all(p.wait()==0 for p in jobs)
 p=subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag',tag,'--out',str(R)],cwd=root,text=True,capture_output=True,check=True)
 (R/f'{tag}-match-summary.json').write_text(p.stdout);print(p.stdout,flush=True)
run('bench_root.py');run('bench_search.py')
