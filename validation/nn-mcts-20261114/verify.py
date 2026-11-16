"""C/packed-PyTorch parity, protocol, rotating equal-time throughput sample."""
import ctypes as C,importlib.util,json,statistics,subprocess,sys
from pathlib import Path
import numpy as np
import torch
from train import Net
ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[1]
spec=importlib.util.spec_from_file_location('h',REPO/'validation/arena-verify/scripts/harness.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)

def library(name,flags=(),baseline=False):
    src=ROOT/'build'/f'{name}.c';so=src.with_suffix('.so')
    code=h.CODE.replace('SOURCE',str(ROOT/('baseline.c' if baseline else 'candidate.c')))
    if not baseline:code+='''
int net_eval(const int *p,float *sx,float *ax,float *logits,float *value){State s=unpack(p);Action aa[ACTIONS];int n=generate(&s,aa);float z[PV_Z];pv_state(&s,sx);*value=pv_encode(&s,z);for(int i=0;i<n;i++){pv_action(&s,aa[i],ax+PV_A*i);logits[i]=pv_logit(&s,aa[i],z);}return n;}
void mc_stats(int *p){p[0]=mc_calls;p[1]=mc_n;p[2]=mc_e;p[3]=mc_simulations;p[4]=mc_max_depth;p[5]=mc_full;int cover=0;for(int i=0;i<mc_nodes[0].count;i++)cover+=mc_edges[i].visits>0;p[6]=cover;p[7]=mc_nodes[0].count;}
void weights(float *p){pv_init();memcpy(p,pv_w,sizeof pv_w);}
'''
    src.write_text(code)
    subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',*['-D'+f for f in flags],str(src),'-lm','-o',str(so)],check=True)
    return C.CDLL(str(so))

def main():
    torch.set_num_threads(2)
    rows=list(map(json.loads,(REPO/'validation/arena-study/results/agree.jsonl').read_text().splitlines()))[:100]
    lib=library('parity');packed=np.load(ROOT/'packed.npy');model=Net();off=0
    with torch.no_grad():
        for param in model.parameters():
            n=param.numel();param.copy_(torch.from_numpy(packed[off:off+n].reshape(param.shape)));off+=n
    model.eval();buf=(C.c_float*len(packed))();lib.weights(buf);assert np.array_equal(np.ctypeslib.as_array(buf),packed)
    sx=(C.c_float*208)();ax=(C.c_float*(253*32))();logits=(C.c_float*253)();value=C.c_float();err=v_err=0.;mismatch=0
    for r in rows:
        h.setup(lib,r['walls']);n=lib.net_eval(h.Array(*r['state']),sx,ax,logits,C.byref(value))
        with torch.no_grad():p,v=model(torch.from_numpy(np.ctypeslib.as_array(sx).copy())[None],torch.from_numpy(np.ctypeslib.as_array(ax)[:n*32].copy().reshape(1,n,32)))
        cc=np.ctypeslib.as_array(logits)[:n];err=max(err,float(np.max(np.abs(cc-p.numpy()[0]))));v_err=max(v_err,abs(value.value-v.item()));mismatch+=int(cc.argmax()!=p.numpy()[0].argmax())
    assert err<1e-4 and v_err<1e-4 and mismatch==0
    parity=dict(positions=len(rows),max_logit_error=err,max_value_error=v_err,top1_disagreements=mismatch,decoded_weights_exact=True)
    (ROOT/'parity.json').write_text(json.dumps(parity,indent=2));print(json.dumps(parity),flush=True)
    variants={'baseline':library('cost-baseline',baseline=True),'puct-value':lib,'puct-heuristic':library('cost-heuristic',['MCTS_VALUE=0']),'ab-value':library('cost-ab',['MCTS_SEARCH=0','AB_NEURAL_VALUE=1'])}
    raw={k:[] for k in variants};names=list(variants)
    for i,r in enumerate(rows):
        for name in names[i%len(names):]+names[:i%len(names)]:
            l=variants[name];h.setup(l,r['walls']);v=h.run(l,r['state'])
            if name.startswith('puct'):
                info=(C.c_int*8)();l.mc_stats(info);v.update(zip(['nn_calls','tree_nodes','edges','simulations','max_depth','pool_full','root_covered','root_actions'],info))
            raw[name].append(v)
    report={}
    for name,vv in raw.items():
        times=sorted(v['ns']/1e6 for v in vv)
        report[name]=dict(positions=len(vv),median_ms=statistics.median(times),p95_ms=times[94],max_ms=max(times),median_visits=statistics.median(v['nodes'] for v in vv))
        if name.startswith('puct'):
            report[name].update(median_nn_calls=statistics.median(v['nn_calls'] for v in vv),median_max_depth=statistics.median(v['max_depth'] for v in vv),median_root_coverage=statistics.median(v['root_covered']/max(1,v['root_actions']) for v in vv),pool_full=sum(v['pool_full'] for v in vv))
    (ROOT/'throughput.json').write_text(json.dumps(report,indent=2));(ROOT/'throughput-raw.json').write_text(json.dumps(raw));print(json.dumps(report),flush=True)
    exe=ROOT/'build/bot';subprocess.run(['cc','-O3','-std=c11',str(ROOT/'candidate.c'),'-lm','-o',str(exe)],check=True)
    protocol=(REPO/'tests/test_protocol.py').read_text().replace("['./bot']",repr([str(exe)]))
    (ROOT/'build/protocol.py').write_text(protocol)
    result=subprocess.run([sys.executable,str(ROOT/'build/protocol.py')],capture_output=True,text=True,check=True)
    (ROOT/'protocol.txt').write_text(result.stdout);print(result.stdout)
if __name__=='__main__':main()
