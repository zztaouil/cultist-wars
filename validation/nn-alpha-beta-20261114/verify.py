"""Packed C++/PyTorch parity, encoder agreement, and inference throughput."""
import ctypes as C, hashlib, json, subprocess, time
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[1]
out=ROOT/'verify';out.mkdir(exist_ok=True)
src=out/'bridge.cpp'
src.write_text('''#define BOT_TEST
#include "'''+str(REPO/'bot.cpp')+'''"
extern "C" void infer(const float*x,float*y,int n){for(int i=0;i<n;++i)y[i]=value_forward(x+239*i);}
extern "C" void encode(const int*w,const int*p,float*x){memcpy(wall,w,sizeof wall);State s{};for(int i=0;i<14;++i)s.u[i]=Unit{(uint8_t)p[4*i],(uint8_t)p[4*i+1],(uint8_t)p[4*i+2],(uint8_t)p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];value_features(&s,x);}
''')
so=out/'bridge.so';subprocess.run(['c++','-std=c++17','-O3','-shared','-fPIC',str(src),'-o',str(so)],check=True)
lib=C.CDLL(str(so));fptr=C.POINTER(C.c_float);iptr=C.POINTER(C.c_int)
lib.infer.argtypes=[fptr,fptr,C.c_int];lib.encode.argtypes=[iptr,iptr,fptr]
data=np.load(ROOT/'trained-v2/data.npz');x=data['x'];pred=np.load(ROOT/'trained-v2/predictions.npy');actual=np.empty(len(x),np.float32)
t=time.monotonic();lib.infer(x.ctypes.data_as(fptr),actual.ctypes.data_as(fptr),len(x));elapsed=time.monotonic()-t
err=float(abs(actual-pred).max());assert err<1e-5
# Round-trip every input field through C++ for sampled archived positions.
for row in x[::97]:
    p=np.zeros(62,np.int32);w=np.array(row[:91],np.int32);p[56]=round(float(row[204]));p[57]=round(float(row[203])*150);p[60]=round(float(row[205]))
    for i in range(14):
        k=91+8*i;p[59]|=int(row[k+7])<<i
        if row[k+2]:
            p[4*i:4*i+4]=[round(float(row[k])*12)+13*round(float(row[k+1])*6),round(float(row[k+2])*10),p[56] if row[k+3] else 1-p[56] if row[k+4] else 2,round(float(row[k+6]))]
    p[58]=sum(int(v)<<b for b,v in enumerate(row[208:]))
    encoded=np.empty(239,np.float32);lib.encode(w.ctypes.data_as(iptr),p.ctypes.data_as(iptr),encoded.ctypes.data_as(fptr));assert np.allclose(encoded,row,atol=1e-7)
report=dict(positions=len(x),max_error=err,encoder_positions=len(x[::97]),inference_microseconds=elapsed*1e6/len(x),source_sha256=hashlib.sha256((REPO/'bot.cpp').read_bytes()).hexdigest())
(ROOT/'parity.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
