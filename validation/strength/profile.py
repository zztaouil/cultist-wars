"""Disjoint quiescence regions; timestamps perturb runtime. No semantics changed."""
import sys,json,tempfile,ctypes as C
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,run
D=Path(__file__).resolve().parent
s=(D/(sys.argv[1] if len(sys.argv)>1 else 'baseline.c')).read_text().replace('(Q_LAZY_EVAL && unstable)', 'unstable');s=s.replace('static int quiescence(', 'static uint64_t costs[7];\nstatic int quiescence(',1)
s=s.replace('int best = evaluate_live(s);','uint64_t stamp=now_ns();\n    int best = evaluate_live(s); costs[0]+=now_ns()-stamp; stamp=now_ns();')
if 'int best = unstable ? -INF' not in s:
 s=s.replace('if (!left || (!unstable && best >= beta)) {','costs[1]+=now_ns()-stamp;\n    if (!left || (!unstable && best >= beta)) {')
s=s.replace('Action actions[ACTIONS]; int n = generate_mode(s, actions, !unstable);','stamp=now_ns();\n    Action actions[ACTIONS]; int n = generate_mode(s, actions, !unstable); costs[2]+=now_ns()-stamp;')
if 'int best = unstable ? -INF' in s:
 s=s.replace('int unstable = use_threat_q', 'uint64_t stamp=now_ns();\n    int unstable = use_threat_q')
 s=s.replace('int best = unstable ? -INF : evaluate_live(s);', 'costs[1]+=now_ns()-stamp; stamp=now_ns();\n    int best = unstable ? -INF : evaluate_live(s); costs[0]+=now_ns()-stamp; stamp=now_ns();')
a=s.index('static int quiescence(');b=s.index('static int search(',a);q=s[a:b]
q=q.replace('int k = i;','stamp=now_ns();\n        int k = i;').replace('Action a = actions[i];','Action a = actions[i]; costs[3]+=now_ns()-stamp;')
q=q.replace('State next = apply(s, a);','stamp=now_ns();\n        State next = apply(s, a); costs[4]+=now_ns()-stamp; costs[5]++;')
s=s[:a]+q+s[b:];s=s.replace('if (!depth) return quiescence(s, alpha, beta, 4);','if (!depth) {uint64_t start=now_ns();int v=quiescence(s,alpha,beta,4);costs[6]+=now_ns()-start;return v;}');s+='\nvoid get_costs(uint64_t *out){memcpy(out,costs,sizeof costs);memset(costs,0,sizeof costs);}\n'
with tempfile.TemporaryDirectory() as td:
 t=Path(td);src=t/'instrumented-source.c';src.write_text(s);lib=build(src,t,'profile')
 ps=json.loads((D.parent/'lmr/positions.json').read_text())+json.loads((D.parent/'lmr/tactics.json').read_text())
 for p in ps:
  setup(lib,p['walls']);r=run(lib,p['state'],5);v=(C.c_uint64*7)();lib.get_costs(v)
  print(json.dumps(dict(name=p['name'],**r,costs=dict(zip(['eval_ns','threat_ns','generate_ns','order_ns','apply_ns','children','q_ns'],v)))),flush=True)
