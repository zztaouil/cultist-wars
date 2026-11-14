"""Same-horizon action scoring; shared fresh history, exact reconstructed RNG.
Only singleton inferred RNG positions qualify. Reference is heuristic, not truth.
"""
import ctypes as C,json,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent
src=(D/'engine.c').read_text()+r'''
int rng_matches(int *p){return tr.n!=1 || (obs.rng==(uint32_t)p[58] && obs.neutral_mask==p[59]);}
int label(int *p,int *a,int depth,int *valid){
State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];
memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(eval_tt,0,sizeof eval_tt);memset(history,0,sizeof history);diag_nn=0;nn_root=0;use_futility=0;use_threat_q=1;
prepare(&s);State child=apply(&s,*(Action*)a);deadline=now_ns()+2000000000ULL;turn_deadline=deadline;stopped=0;nodes=qnodes=0;Action best;int v=search(&child,depth,-INF,INF,&best);*valid=!stopped;return -v;}
'''
(D/'label-engine.c').write_text(src)
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/'label-engine.c'),'-o',str(D/'label-engine.so')],check=True)
lib=C.CDLL(str(D/'label-engine.so'));A=C.c_int*62;M=C.c_int*4
corpus={(x['game'],x['turn']):x for x in json.loads((D/'corpus.json').read_text())}
rows=[json.loads(x) for x in (D/'results.jsonl').read_text().splitlines()]
checked=0
for p in corpus.values():
 lib.setup((C.c_int*91)(*p['walls']))
 for t in range(1,p['turn']+1,2):
  lib.observe(A(*p['states'][t]));assert lib.rng_matches(A(*p['states'][t])),(p['game'],t)
  lib.actual(M(*p['actions'][t],0));checked+=1
(D/'tracker-check.json').write_text(json.dumps({'observations_checked':checked,'singleton_rng_mismatches':0}))
with (D/'labels.jsonl').open('w') as f:
 for r in rows:
  if r['rng_candidates']!=1 or 'own_material_loss' not in r['kinds']:continue
  choices={tuple(r['actual']),tuple(r['results']['nn49']['action']),tuple(r['results']['nn500']['action']),tuple(r['results']['off49']['action'])}
  if len(choices)==1:continue
  p=corpus[r['game'],r['turn']];lib.setup((C.c_int*91)(*p['walls']))
  scores=[]
  for a in sorted(choices):
   values=[]
   for depth in (3,5,7):
    valid=C.c_int();v=lib.label(A(*p['states'][-1]),M(*a,0),depth,C.byref(valid));values.append(v if valid.value else None)
   scores.append(dict(action=a,depth4=values[0],depth6=values[1],depth8=values[2]))
  item=dict(game=r['game'],turn=r['turn'],scores=scores)
  f.write(json.dumps(item)+'\n');f.flush()
  print(r['game'],r['turn'],flush=True)
