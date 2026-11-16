import ctypes as C, json, subprocess, time
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent; OLD=R.parent/'nn-41710886'; B=R/'build';B.mkdir(exist_ok=True)
(B/'wide-group-parity.c').write_text('''#define BOT_TEST
#include "../wide-group.c"
void score(const float*f,float*out,int n){nn_init();for(int i=0;i<n;i++)out[i]=nn_score(f+i*128);}
void params(float*out){nn_init();memcpy(out,nn_params,sizeof nn_params);}
''')
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(B/'wide-group-parity.c'),'-o',str(B/'wide-group-parity.so')],check=True)
lib=C.CDLL(str(B/'wide-group-parity.so'));lib.score.argtypes=[C.c_void_p,C.c_void_p,C.c_int];lib.params.argtypes=[C.c_void_p]
packed=np.load(R/'wide_group_packed_weights.npy');actual=np.empty_like(packed);lib.params(actual.ctypes.data);assert np.array_equal(packed,actual)
w=json.loads((R/'fine-wide/policy_weights.json').read_text()); w={k:np.array(v,np.float32) for k,v in w.items()};q={};offset=0
for k,v in w.items():
 q[k]=packed[offset:offset+v.size].reshape(v.shape);offset+=v.size
assert offset==len(packed)
def forward(f,weights):
    for i in (0,2,4):
        f=f@weights[f'{i}.weight'].T+weights[f'{i}.bias']
        if i<4:f=np.maximum(f,0)
    return f.flatten()
x=np.memmap(R.parent/'imitation-5m-20261114/cache/features.f32',dtype='<f4',mode='r').reshape(-1,128)
m=json.loads((R.parent/'imitation-5m-20261114/cache/meta.json').read_text());ids=[i for i,s in enumerate(m['splits']) if s in (1,2)]
maxerr=quant_err=0.;disagree=quant_disagree=correct=0
metrics={1:dict(positions=0,top1=0,top3=0,loss=0.),2:dict(positions=0,top1=0,top3=0,loss=0.)}
decisions=[]
for i in ids:
    f=np.ascontiguousarray(x[m['offsets'][i]:m['offsets'][i+1]]);ref=forward(f,q);full=forward(f,w);out=np.empty(len(f),np.float32)
    lib.score(f.ctypes.data,out.ctypes.data,len(f));maxerr=max(maxerr,float(np.max(abs(ref-out))))
    quant_err=max(quant_err,float(np.max(abs(full-out))));disagree+=int(ref.argmax()!=out.argmax());quant_disagree+=int(full.argmax()!=out.argmax());correct+=int(out.argmax()==m['labels'][i])
    row=metrics[m['splits'][i]];label=m['labels'][i];row['positions']+=1
    row['top1']+=int(out.argmax()==label);row['top3']+=int(label in np.argsort(out)[-3:])
    z=out.astype(np.float64);ce=float(np.log(np.exp(z-z.max()).sum())+z.max()-z[label]);row['loss']+=ce
    decisions.append((i,int(out.argmax()),int(label in np.argsort(out)[-3:]),ce))
np.save(R/'wide-group-decisions.npy',np.asarray(decisions))
for row in metrics.values():
 for key in ('top1','top3','loss'):row[key]/=row['positions']
assert maxerr<.0002 and disagree<=2
report=dict(groups=len(ids),decoded_params_exact=True,max_c_vs_int8_numpy_error=maxerr,c_vs_int8_top1_disagreements=disagree,max_c_vs_fp32_error=quant_err,fp32_vs_int8_top1_disagreements=quant_disagree,int8_test_top1=correct/len(ids))
report.update(validation=metrics[1],test=metrics[2]);report['int8_test_top1']=metrics[2]['top1']
(R/'wide-group-parity.json').write_text(json.dumps(report,indent=2));print(report)
# Existing full regression suite against isolated candidate.
test=(R.parents[1]/'tests/test_bot.c').read_text().replace('#include "../bot.c"','#include "../wide-group.c"');(B/'wide-group-test.c').write_text(test)
for flags,name in [(['-O2'],'test'),(['-O1','-g','-fsanitize=address,undefined'],'sanitize')]:
    subprocess.run(['cc','-std=c11',*flags,str(B/'wide-group-test.c'),'-o',str(B/name)],check=True)
    subprocess.run([str(B/name)],check=True)
subprocess.run(['cc','-O3','-std=c11',str(R/'wide-group.c'),'-o',str(B/'wide-group')],check=True)
