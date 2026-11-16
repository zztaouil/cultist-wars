import json,ctypes as C,subprocess,sys,statistics
from pathlib import Path
D=Path(__file__).resolve().parent;root=D.parents[1]
sys.path.insert(0,str(root/'validation/arena-verify/scripts'));import harness as h
libs=[]
for tag in ['baseline','selected']:
 code=h.CODE.replace('SOURCE',str(D/(tag+'.c')))+'''
uint64_t eval_cost(const int *p,int n){State s=unpack(p);prepare(&s);volatile int sink=0;uint64_t start=now_ns();for(int i=0;i<n;i++){eval_tt[(s.key^s.metadata)&(TT_SIZE-1)].valid=0;sink+=evaluate_live(&s);}return now_ns()-start;}
'''
 c=D/f'measure-{tag}.c';c.write_text(code);so=D/f'measure-{tag}.so';subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC','-DBUDGET_MS=48',str(c),'-o',str(so)],check=True);lib=C.CDLL(str(so));lib.eval_cost.restype=C.c_uint64;libs.append(lib)
games=json.loads((D.parent/'arena-loss-audit-20261114/reconstructed.json').read_text());rows=[]
for g in games[:16]:
 for lib in libs:h.setup(lib,g['walls'])
 for t in [20,40]:
  s=g['states'][min(t,len(g['states'])-1)];r={'game':g['id'],'turn':t}
  for k in [0,1] if t==20 else [1,0]:
   lib=libs[k];tag=['baseline','selected'][k];r[tag]=h.run(lib,s);r[tag]['eval_ns']=lib.eval_cost(h.Array(*s),10000)/10000
  rows.append(r)
(D/'performance.json').write_text(json.dumps(rows,indent=2))
for tag in ['baseline','selected']:
 print(tag,{k:round(statistics.mean(r[tag][k] for r in rows),2) for k in ['depth','nodes','ns','eval_ns']},'max_ns',max(r[tag]['ns'] for r in rows))
print('changed moves',sum(r['baseline']['action']!=r['selected']['action'] for r in rows),'/',len(rows))
