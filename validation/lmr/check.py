"""Exact disabled ablation plus unchanged legal transitions and cache invariants."""
import ctypes as C, json, tempfile
from pathlib import Path
from run import D, ROOT, build, setup, run, Array, Move
with tempfile.TemporaryDirectory() as td:
    td=Path(td);a=build(D/'baseline.c',td,'base');b=build(D/'experiment.c',td,'disabled')
    cases=json.loads((D/'positions.json').read_text())+json.loads((D/'tactics.json').read_text())
    for p in cases:
        out=[]
        for lib in (a,b):
            setup(lib,p['walls']);out.append(run(lib,p['state'],5,0))
        for key in ('value','action','nodes','qnodes','stopped'):assert out[0][key]==out[1][key],(p['name'],key,out)
    print(json.dumps({'disabled_exact_positions':len(cases),'depth':5,'matched':['value','action','nodes','qnodes','stopped']}))
