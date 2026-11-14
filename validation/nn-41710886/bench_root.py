import ctypes as C,json,subprocess,statistics
from pathlib import Path
R=Path(__file__).resolve().parent
src='''#define BOT_TEST
#include "../candidate.c"
void setup(const int*w){memcpy(wall,w,sizeof wall);initialize();}
unsigned long long bench(const int*p){State s={0};for(int i=0;i<14;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);uint64_t t=now_ns();nn_begin(&s);uint64_t elapsed=now_ns()-t;
for(int i=0;i<nn_count;i++){if(nn_order(nn_actions[i])!=nn_ranks[i])return 0;for(int j=i+1;j<nn_count;j++)if(nn_ranks[i]==nn_ranks[j])return 0;}nn_root=0;return elapsed;}
'''
(R/'build/root_bench.c').write_text(src);subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(R/'build/root_bench.c'),'-o',str(R/'build/root_bench.so')],check=True)
lib=C.CDLL(str(R/'build/root_bench.so'));lib.bench.restype=C.c_uint64
samples=[]
for row in list(map(json.loads,open('validation/arena-study/results/agree.jsonl')))[:500]:
 lib.setup((C.c_int*91)(*row['walls']));ns=lib.bench((C.c_int*62)(*row['state']));assert ns>0;samples.append(ns/1e6)
samples.sort();r={'positions':len(samples),'root_precompute_median_ms':statistics.median(samples),'root_precompute_p95_ms':samples[int(.95*len(samples))],'root_precompute_max_ms':max(samples),'includes':'action generation, feature extraction, NN inference, rank cache; concurrent match workers active'}
(R/'root-cost.json').write_text(json.dumps(r,indent=2));print(r)
