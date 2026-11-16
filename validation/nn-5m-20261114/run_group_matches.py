"""Validation-selected grouped policy and its cached tree-ordering ablation."""
import json,subprocess,sys
from pathlib import Path
from match_guard import guard
R=Path(__file__).resolve().parent;root=R.parents[1];B=R/'build'
def run(name,*args):subprocess.run([sys.executable,str(R/name),*args],cwd=root,check=True)
losses={n:json.loads((R/f).read_text())['validation']['loss'] for n,f in [('candidate','parity.json'),('wide','wide-parity.json'),('wide-group','wide-group-parity.json')]}
selected=min(losses,key=losses.get);assert selected=='wide-group',losses
(R/'final-selection.json').write_text(json.dumps(dict(selected=selected,quantized_validation_losses=losses,criterion='Minimum quantized validation cross-entropy; test and match outcomes not used.',initial_selection='selection.json'),indent=2))
run('tree_variant.py','wide-group.c','group-tree.c');run('verify_group_tree.py')
protocol=(root/'tests/test_protocol.py').read_text();results={}
for name in ['wide-group','group-tree']:
 binary=B/name;subprocess.run(['cc','-O3','-std=c11',str(R/f'{name}.c'),'-o',str(binary)],check=True)
 p=subprocess.run([sys.executable,'-c',protocol.replace("'./bot'",repr(str(binary)))],text=True,capture_output=True,check=True);results[name]=p.stdout.strip();print(name,p.stdout.strip(),flush=True)
test=(root/'tests/test_bot.c').read_text().replace('#include "../bot.c"','#include "../group-tree.c"');(B/'group-tree-test.c').write_text(test)
subprocess.run(['cc','-O1','-g','-std=c11','-fsanitize=address,undefined',str(B/'group-tree-test.c'),'-o',str(B/'group-tree-sanitize')],check=True);subprocess.run([str(B/'group-tree-sanitize')],check=True)
(R/'group-runtime.json').write_text(json.dumps(results,indent=2))
for tag,a,b in [('group49','baseline','wide-group'),('grouptree49','wide-group','group-tree')]:
 guard(tag,R/f'{a}.c',R/f'{b}.c')
 jobs=[]
 for part in range(4):
  cmd=[sys.executable,'tests/arena_match.py','--a',str(R/f'{a}.c'),'--b',str(R/f'{b}.c'),'--tag',tag,'--out',str(R),'--maps',str(R/'match-maps.json'),'--maps-used','64','--budget-ms','49','--shuffle-seed','41723000','--parts','4','--part',str(part)]
  jobs.append(subprocess.Popen(cmd,cwd=root))
 assert all(p.wait()==0 for p in jobs)
 p=subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag',tag,'--out',str(R)],cwd=root,text=True,capture_output=True,check=True)
 (R/f'{tag}-match-summary.json').write_text(p.stdout);print(p.stdout,flush=True)
run('bench_group.py')
print('Grouped-policy evaluation complete.',flush=True)
