import sys,json,tempfile,ctypes as C
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,Array
D=Path(__file__).resolve().parent
extra='''
uint64_t eval_bench(const int *p,int loops,int cold,int *sum){
 State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];prepare(&s);
 uint64_t start=now_ns();int v=0;for(int i=0;i<loops;i++){if(cold)eval_tt[EVAL_INDEX].valid=0;v+=evaluate_live(&s);}*sum=v;return now_ns()-start;
}
'''
with tempfile.TemporaryDirectory() as td:
 t=Path(td);libs=[]
 for name in ['baseline','eval-hp','eval-race']:
  src=t/(name+'-source.c');src.write_text((D/(name+'.c')).read_text()+extra.replace('EVAL_INDEX','((s.key^s.metadata)^mix64(0xabc123ULL+s.side))&(TT_SIZE-1)' if name=='eval-race' else '(s.key^s.metadata)&(TT_SIZE-1)'));lib=build(src,t,name);lib.eval_bench.restype=C.c_uint64;libs.append(lib)
 for rep in range(5):
  for p in json.loads((D.parent/'lmr/positions.json').read_text()):
   for k in (range(len(libs)) if rep%2==0 else reversed(range(len(libs)))):
    setup(libs[k],p['walls'])
    for cold in (0,1):
     total=C.c_int();ns=libs[k].eval_bench(Array(*p['state']),100000,cold,C.byref(total));print(json.dumps(dict(repeat=rep,position=p['name'],variant=k,cold=cold,ns=ns,calls=100000,checksum=total.value)),flush=True)
