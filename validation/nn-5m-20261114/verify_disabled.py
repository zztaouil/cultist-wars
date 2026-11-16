import ctypes as C, importlib.util, json, subprocess
from pathlib import Path
R=Path(__file__).resolve().parent; root=R.parents[1]
spec=importlib.util.spec_from_file_location('h',root/'validation/arena-verify/scripts/harness.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
libs=[]
for variant in ('baseline','candidate','wide'):
    path=R/'build'/f'off-{variant}.c';path.write_text('#define NN_ENABLE 0\n'+h.CODE.replace('SOURCE',str(R/f'{variant}.c')))
    so=path.with_suffix('.so');subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(path),'-o',str(so)],check=True);libs.append(C.CDLL(str(so)))
rows=list(map(json.loads,(root/'validation/arena-study/results/agree.jsonl').read_text().splitlines()))[:100]
for row in rows:
    results=[]
    for lib in libs:
        h.setup(lib,row['walls']);r=h.run(lib,row['state'],4);r.pop('ns');results.append(r)
    assert all(v==results[0] for v in results[1:]),results
report=dict(positions=len(rows),depth=4,disabled_policy_exact=True)
(R/'disabled-parity.json').write_text(json.dumps(report,indent=2));print(report)
