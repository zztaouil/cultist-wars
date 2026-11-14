from pathlib import Path
import ctypes as C,json,zipfile,subprocess,time,hashlib
import numpy as np
R=Path(__file__).resolve().parent
wrapper='''#define BOT_TEST
#include "../candidate.c"
void score(const float*f,float*out,int n){for(int i=0;i<n;i++)out[i]=nn_score(f+i*128);}
'''
(R/'build/parity.c').write_text(wrapper)
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(R/'build/parity.c'),'-o',str(R/'build/parity.so')],check=True)
lib=C.CDLL(str(R/'build/parity.so'));lib.score.argtypes=[C.c_void_p,C.c_void_p,C.c_int]
x=np.fromfile(R/'cache/features.f32',dtype=np.float32).reshape(-1,128)
with zipfile.ZipFile(R/'cultist-nn-results.zip') as z:w=json.loads(z.read('policy_weights.json'))
a=np.array(w['0.weight'],np.float32);b=np.array(w['0.bias'],np.float32);o=np.array(w['2.weight'],np.float32).reshape(-1);ob=w['2.bias'][0]
# Fixed random sample plus complete groups for action-rank parity.
m=json.loads((R/'cache/meta.json').read_text());ids=np.random.default_rng(7).choice(len(m['labels']),1000,replace=False)
maxerr=0;disagree=0;elapsed=[]
for i in ids:
 f=np.ascontiguousarray(x[m['offsets'][i]:m['offsets'][i+1]]);ref=np.maximum(f@a.T+b,0)@o+ob;out=np.empty(len(f),np.float32)
 t=time.perf_counter_ns();lib.score(f.ctypes.data,out.ctypes.data,len(f));elapsed.append((time.perf_counter_ns()-t)/1e6)
 maxerr=max(maxerr,float(np.max(np.abs(ref-out))));disagree+=int(np.argmax(ref)!=np.argmax(out))
assert maxerr<0.0001 and disagree==0
report={'groups':1000,'max_abs_logit_error':maxerr,'top1_disagreements':disagree,'inference_only_p95_ms':float(np.percentile(elapsed,95))}
(R/'parity.json').write_text(json.dumps(report,indent=2));print(report)
# Map groups held out from model training and checkpoint selection.
maps=json.loads(Path('validation/arena-verify/results/maps.json').read_text());hold=[]
for row in maps:
 wall=row[1];rows=[wall[i:i+13] for i in range(0,91,13)];variants=[sum(r,[]) for r in (rows,rows[::-1],[a[::-1] for a in rows],[a[::-1] for a in rows[::-1]])];key=hashlib.sha256(bytes(min(variants))).hexdigest()
 if int(key[:8],16)%100>=90:hold.append(row)
(R/'match-maps.json').write_text(json.dumps(hold));print('Held-out maps:',len(hold))
