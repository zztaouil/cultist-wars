import ctypes as C,subprocess
from pathlib import Path
Array=C.c_int*62;Move=C.c_int*4
CODE='''#define BOT_TEST
#include "SOURCE"
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();}
int run(const int *p,int depth,int *out,uint64_t *stats){
State s=unpack(p);uint64_t start=now_ns();turn_deadline=start+(uint64_t)BUDGET_MS*1000000ULL;
Action a={WAIT,0,0,0};int value,completed=depth;
if(depth){prepare(&s);nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;deadline=start+30000000000ULL;value=search(&s,depth,-INF,INF,&a);}
else a=choose(&s,&completed,&value);
memcpy(out,&a,sizeof a);stats[0]=completed;stats[1]=nodes;stats[2]=qnodes;stats[3]=now_ns()-start;
return value;}
int score_child(const int *p,const int *act,int depth){State s=unpack(p);prepare(&s);State c=apply(&s,*(const Action*)act);
uint64_t start=now_ns();deadline=start+30000000000ULL;turn_deadline=deadline;stopped=0;Action a;int v;
if(terminal(&c,&v))return -v; prepare(&c);return -search(&c,depth,-INF,INF,&a);}
'''
def build(src,name,budget=49):
  p=Path('build');p.mkdir(exist_ok=True);c=p/(name+'.c');c.write_text(CODE.replace('SOURCE',str(Path(src).resolve())))
  so=p/(name+'.so');subprocess.run(['cc','-O3','-march=native','-std=c11','-shared','-fPIC',f'-DBUDGET_MS={budget}',str(c),'-o',str(so)],check=True)
  return C.CDLL(str(so.resolve()))
def setup(lib,w):lib.setup((C.c_int*91)(*w))
def run(lib,s,depth=0):
  a=Move();st=(C.c_uint64*4)();v=lib.run(Array(*s),depth,a,st);return dict(value=v,action=list(a)[:3],depth=st[0],nodes=st[1],ns=st[3])
CODE2='''
void step(int *p,const int *a){State s=unpack(p);s=apply(&s,*(const Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
int static_eval(const int *p){State s=unpack(p);return evaluate(&s);}
'''
CODE=CODE+CODE2
def step(lib,s,a):
  x=Array(*s);lib.step(x,Move(*a,0));return list(x)
def child(lib,s,a,d):return lib.score_child(Array(*s),Move(*a,0),d)
