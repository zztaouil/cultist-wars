"""Frozen-search residual pilot. All generated files stay in this directory."""
import ctypes as C, hashlib, importlib.util, json, subprocess
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[1]
spec=importlib.util.spec_from_file_location('h',REPO/'validation/arena-verify/scripts/harness.py')
h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
def candidate():
    s=(ROOT/'baseline.c').read_text()
    anchor='static int quiescence(const State *s, int alpha, int beta, int left) {'
    assert s.count(anchor)==1
    s=s.replace(anchor,'#include "residual.h"\n'+anchor)
    assert s.count('int best = unstable ? -INF : evaluate_live(s);')==1
    s=s.replace('int best = unstable ? -INF : evaluate_live(s);','int best = unstable ? -INF : residual_leaf(s);')
    (ROOT/'candidate.c').write_text(s)
    embedded=(ROOT/'residual.h').read_text().replace('#include "weights.h"',(ROOT/'weights.h').read_text())
    (ROOT/'standalone.c').write_text(s.replace('#include "residual.h"',embedded))
def library(name,enabled=0,sample=False):
    build=ROOT/'build';build.mkdir(exist_ok=True)
    src=ROOT/'candidate.c'
    extra=''
    if sample:
        text=(ROOT/'candidate.c').read_text().replace('#include "residual.h"','#include "residual.h"\nstatic State samples[8];static int seen,nsamples;\nstatic int sampled(const State*s){if(!material_threat(s)){seen++;if(nsamples<8)samples[nsamples++]=*s;else {unsigned k=mix64(s->key^seen)%seen;if(k<8)samples[k]=*s;}}return evaluate_live(s);}')
        text=text.replace(' : residual_leaf(s);',' : sampled(s);')
        src=ROOT/'sampler.c';src.write_text(text)
        extra='''
void reset_samples(void){seen=nsamples=0;}
int sample_count(void){return nsamples;}
void get_sample(int i,int*p){State s=samples[i];for(int j=0;j<14;j++){p[4*j]=s.u[j].pos;p[4*j+1]=s.u[j].hp;p[4*j+2]=s.u[j].owner;p[4*j+3]=s.u[j].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
'''
    code=h.CODE.replace('SOURCE',str(src))+'''
void get_features(const int*p,float*f){State s=unpack(p);prepare(&s);residual_features(&s,f);}
int correction(const int*p){State s=unpack(p);prepare(&s);return residual_correction(&s);}
unsigned long calls(void){return residual_calls;}
int quiet(const int*p){State s=unpack(p);prepare(&s);int v;return !terminal(&s,&v)&&!material_threat(&s);}
int label(const int*p,int*out){State s=unpack(p);prepare(&s);deadline=now_ns()+100000000ULL;turn_deadline=deadline;stopped=0;nodes=0;Action a;int v=search(&s,2,-INF,INF,&a);*out=v;return !stopped;}
'''+extra
    c=build/(name+'.c');c.write_text(code);so=c.with_suffix('.so')
    subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',f'-DRESIDUAL_ENABLE={enabled}',str(c),'-o',str(so)],check=True)
    return C.CDLL(str(so))
def map_key(w):
    variants=[bytes(w[(6-y if t&2 else y)*13+(12-x if t&1 else x)] for y in range(7) for x in range(13)) for t in range(4)]
    return hashlib.sha256(min(variants)).hexdigest()
def save(name,value): (ROOT/name).write_text(json.dumps(value,indent=2))
if __name__=='__main__':candidate()
