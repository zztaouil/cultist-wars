"""Compile frozen simulator plus shared features for training and verification."""
import ctypes as C
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
I=C.c_int; F=C.c_float
State=I*62; Move=I*4; Walls=I*91

def build():
    out=ROOT/'build';out.mkdir(exist_ok=True)
    src=out/'features.c'
    src.write_text('''#define BOT_TEST
#include "../baseline.c"
#include "../pv.h"
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();}
int extract(const int *p,const int *a,float *state,float *actions,int *moves){State s=unpack(p);Action aa[ACTIONS];int n=generate(&s,aa);pv_state(&s,state);int label=-1;for(int i=0;i<n;i++){pv_action(&s,aa[i],actions+i*PV_A);moves[i*3]=aa[i].kind;moves[i*3+1]=aa[i].unit;moves[i*3+2]=aa[i].target;if(aa[i].kind==a[0]&&(!a[0]||(aa[i].unit==a[1]&&aa[i].target==a[2])))label=i;}return label<0?-1:n;}
int verify(const int *p,const int *a,const int *q){State s=unpack(p),want=unpack(q);Action aa={a[0],a[1],a[2],0};s=apply(&s,aa);return same_board(&s,&want)&&s.side==want.side&&s.remaining==want.remaining&&s.rng==want.rng&&s.neutral_mask==want.neutral_mask;}
int is_terminal(const int *p){State s=unpack(p);int v;return terminal(&s,&v);}
''')
    so=out/'features.so'
    subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(src),'-lm','-o',str(so)],check=True)
    lib=C.CDLL(str(so))
    lib.setup.argtypes=[C.POINTER(I)]
    lib.extract.argtypes=[C.POINTER(I),C.POINTER(I),C.POINTER(F),C.POINTER(F),C.POINTER(I)]
    lib.verify.argtypes=[C.POINTER(I)]*3;lib.is_terminal.argtypes=[C.POINTER(I)]
    return lib
