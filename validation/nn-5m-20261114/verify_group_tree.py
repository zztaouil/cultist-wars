import ctypes as C, importlib.util, json, subprocess
from pathlib import Path
R=Path(__file__).resolve().parent; root=R.parents[1]
spec=importlib.util.spec_from_file_location('h',root/'validation/arena-verify/scripts/harness.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
libs=[]
for variant in ('wide-group','group-tree'):
    path=R/'build'/f'off-{variant}.c';path.write_text(h.CODE.replace('value=search(&s,depth', 'use_futility=0;nn_begin(&s);value=search(&s,depth').replace('SOURCE',str(R/f'{variant}.c')))
    so=path.with_suffix('.so');subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(path),'-o',str(so)],check=True);libs.append(C.CDLL(str(so)))
rows=list(map(json.loads,(root/'validation/arena-study/results/agree.jsonl').read_text().splitlines()))[:100]
for row in rows:
    results=[]
    for lib in libs:
        h.setup(lib,row['walls']);r=h.run(lib,row['state'],4);r.pop('ns');results.append(r)
    assert results[0]['value']==results[1]['value'],results
report=dict(positions=len(rows),depth=4,tree_full_width_value_exact=True)
(R/'group-tree-parity.json').write_text(json.dumps(report,indent=2));print(report)
