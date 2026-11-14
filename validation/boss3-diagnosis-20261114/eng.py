"""Diagnostic engine built from frozen submission 41711135 (sha 902527dc...). Production source untouched.
Runtime switches: diag_lhp (extra eval points per leader HP, both sides), diag_nn (root NN ordering)."""
import ctypes as C,subprocess,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;SRC=D.parent/'nn-41710886/arena/41711135.c'
src=SRC.read_text();assert hashlib.sha256(src.encode()).hexdigest().startswith('902527dc')
a='score[side] += 1000 + 18 * u->hp + (u->leader ? 420 : 0);';assert src.count(a)==1
src=src.replace(a,'score[side] += 1000 + 18 * u->hp + (u->leader ? 420 + diag_lhp * u->hp : 0);')
src=src.replace('static int evaluate_live(const State *s) {','static int diag_lhp=0;\nstatic int evaluate_live(const State *s) {',1)
src=src.replace('if(!NN_ENABLE)return;','if(!diag_nn)return;').replace('static const State *nn_root;','static int diag_nn=1;\nstatic const State *nn_root;')
W=r'''
static State unpack(int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();}
static void fresh(int lhp,long ms){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(eval_tt,0,sizeof eval_tt);memset(history,0,sizeof history);diag_lhp=lhp;nn_root=0;nodes=qnodes=0;stopped=0;deadline=now_ns()+(uint64_t)ms*1000000ULL;turn_deadline=deadline;}
int legal(int *p,int *out){State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);for(int i=0;i<n;i++){int v=-1,d=0;if(a[i].kind==SHOOT){v=fast_hit(&s,s.u[a[i].unit].pos,s.u[a[i].target].pos);if(v>=0)d=7-taxi[s.u[a[i].unit].pos][s.u[v].pos];}out[5*i]=a[i].kind;out[5*i+1]=a[i].unit;out[5*i+2]=a[i].target;out[5*i+3]=v;out[5*i+4]=d;}return n;}
int static_eval(int *p,int lhp){State s=unpack(p);fresh(lhp,1000);return evaluate(&s);}
/* value of each root action at fixed total depth, full window, production pruning; caches shared across children only */
int child_values(int *p,int depth,int lhp,long ms_cap,int *acts,int *vals){State s=unpack(p);fresh(lhp,ms_cap);Action a[ACTIONS];int n=generate(&s,a);
 for(int i=0;i<n;i++){State c=apply(&s,a[i]);Action b;int v=-search(&c,depth-1,-INF,INF,&b);if(stopped)return -1;acts[3*i]=a[i].kind;acts[3*i+1]=a[i].unit;acts[3*i+2]=a[i].target;vals[i]=v;}return n;}
int timed(int *p,int ms,int lhp,int nn,int *out,long long *m){State s=unpack(p);fresh(lhp,ms);diag_nn=nn;int d,v;Action b=choose(&s,&d,&v);out[0]=b.kind;out[1]=b.unit;out[2]=b.target;m[0]=d;m[1]=nodes;m[2]=qnodes;return v;}
int fixed(int *p,int depth,int lhp,long ms_cap,int *out){State s=unpack(p);fresh(lhp,ms_cap);Action b={0};int v=search(&s,depth,-INF,INF,&b);if(stopped)return -9999999;out[0]=b.kind;out[1]=b.unit;out[2]=b.target;return v;}
int one(int *p,int *a,int depth,int lhp,long ms_cap){State s=unpack(p);fresh(lhp,ms_cap);Action x={a[0],a[1],a[2],0};State c=apply(&s,x);Action b;int v=-search(&c,depth-1,-INF,INF,&b);return stopped?-9999999:v;}
void step(int *p,int *a){State s=unpack(p);Action x={a[0],a[1],a[2],0};s=apply(&s,x);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
'''
(D/'diag-engine.c').write_text('#define BOT_TEST\n'+src+W)
so=D/'diag-engine.so'
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/'diag-engine.c'),'-o',str(so)],check=True)
lib=C.CDLL(str(so));A=C.c_int*62
lib.timed.argtypes=[C.POINTER(C.c_int),C.c_int,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_longlong)]
lib.child_values.argtypes=[C.POINTER(C.c_int),C.c_int,C.c_int,C.c_long,C.POINTER(C.c_int),C.POINTER(C.c_int)]
def setup(walls):lib.setup((C.c_int*91)(*[int(c) for c in walls]))
def P(s):return A(*s)
def legal(s):
    out=(C.c_int*(5*240))();n=lib.legal(P(s),out);return [tuple(out[5*i:5*i+5]) for i in range(n)]
def child_values(s,depth,lhp=0,cap=20000):
    acts=(C.c_int*(3*240))();vals=(C.c_int*240)();n=lib.child_values(P(s),depth,lhp,cap,acts,vals)
    return None if n<0 else {tuple(acts[3*i:3*i+3]):vals[i] for i in range(n)}
def timed(s,ms=49,lhp=0,nn=1):
    out=(C.c_int*3)();m=(C.c_longlong*3)();v=lib.timed(P(s),ms,lhp,nn,out,m);return tuple(out),v,m[0],m[1]
lib.fixed.argtypes=[C.POINTER(C.c_int),C.c_int,C.c_int,C.c_long,C.POINTER(C.c_int)]
lib.one.argtypes=[C.POINTER(C.c_int),C.POINTER(C.c_int),C.c_int,C.c_int,C.c_long]
def fixed(s,depth,lhp=0,cap=30000):
    out=(C.c_int*3)();v=lib.fixed(P(s),depth,lhp,cap,out);return (None,None) if v==-9999999 else (tuple(out),v)
def one(s,a,depth,lhp=0,cap=30000):
    v=lib.one(P(s),(C.c_int*3)(*a[:3]),depth,lhp,cap);return None if v==-9999999 else v
def step(s,a):
    p=P(s);lib.step(p,(C.c_int*3)(*a));return list(p)
def cmd(a):
    k,i,t=a[:3];return 'WAIT' if not k else '%d MOVE %d %d'%(i,t%13,t//13) if k==1 else '%d %s %d'%(i,'SHOOT' if k==2 else 'CONVERT',t)
