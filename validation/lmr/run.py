"""Reproducible local profiling, tactical reference, and paired matches."""
import argparse, ctypes as C, json, random, subprocess, tempfile, time
from pathlib import Path
D=Path(__file__).resolve().parent
ROOT=D.parent.parent
# Reuse existing map generator, without running its CLI or benchmark.
ns={'__file__':str(ROOT/'tests/benchmark.py')}
src=(ROOT/'tests/benchmark.py').read_text().split('with tempfile.TemporaryDirectory')[0]
src=src.replace('args = parser.parse_args()', 'args = None')
exec(src,ns)
fixture=ns['fixture']; Array=C.c_int*62; Move=C.c_int*4
names=['main','generated','expanded','searched','beta','first','qleft','unstable','unstable_moves','qtt_cut','tt_cut','pvs','pvs_nodes','aspiration','aspiration_nodes','q_ns','threat_calls','threat_ns','screen_calls','screen_ns','reductions','researches','research_nodes']
class Metrics(C.Structure):
    _fields_=[(n,C.c_uint64*(33 if n in names[:6] else 5) if n in names[:7] else C.c_uint64) for n in names]
def build(source,tmp,name,profile=1,lmr=0,budget=49):
    instrumented='Metrics;' in source.read_text()
    extra='use_lmr=mode;profile_clock=timing;memset(&metrics,0,sizeof metrics);' if instrumented else ''
    code='''#define BOT_TEST
#include "SOURCE"
static Tracker tracker;static int turns;
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tracker,0,sizeof tracker);turns=0;}
int run(const int *p,int depth,int mode,int timing,int observe,int *out,uint64_t *stats){
State s=unpack(p);EXTRA
uint64_t start=now_ns();turn_deadline=start+(uint64_t)BUDGET_MS*1000000ULL;
if(observe)track_observation(&tracker,&s,turns++==0);
Action a={WAIT,0,0,0};int value,completed=depth;
if(depth){prepare(&s);nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;deadline=start+30000000000ULL;value=search(&s,depth,-INF,INF,&a);}
else a=choose(&s,&completed,&value);
if(observe)track_action(&tracker,a);
memcpy(out,&a,sizeof a);stats[0]=completed;stats[1]=nodes;stats[2]=qnodes;stats[3]=now_ns()-start;stats[4]=stopped;
return value;}
void step(int *p,const int *a){State s=unpack(p);s=apply(&s,*(const Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
int ended(const int *p){State s=unpack(p);int v;return terminal(&s,&v);}
'''.replace('SOURCE',str(source)).replace('EXTRA',extra)
    if instrumented:code+='\nvoid metrics_copy(void *p){memcpy(p,&metrics,sizeof metrics);}\n'
    path=tmp/(name+'.c');path.write_text(code);lib=tmp/(name+'.so')
    subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',f'-DPROFILE={profile}',f'-DLMR={lmr}',f'-DBUDGET_MS={budget}',str(path),'-o',str(lib)],check=True)
    return C.CDLL(str(lib))
def setup(lib,w):lib.setup((C.c_int*91)(*w))
def run(lib,s,depth=0,mode=0,timing=0,observe=0):
    a=Move();st=(C.c_uint64*5)();v=lib.run(Array(*s),depth,mode,timing,observe,a,st)
    r=dict(value=v,action=list(a)[:3],depth=st[0],nodes=st[1],qnodes=st[2],ns=st[3],stopped=st[4])
    if hasattr(lib,'metrics_copy'):
        m=Metrics();lib.metrics_copy(C.byref(m));r['metrics']={n:list(getattr(m,n)) if n in names[:7] else getattr(m,n) for n in names}
    return r

def save_positions(tmp):
    lib=build(D/'baseline.c',tmp,'fixture',budget=3);items=[]
    for seed in range(500,504):
        s,w=fixture(seed);setup(lib,w)
        for turn in range(150):
            if lib.ended(s):break
            if turn in [0,12,30,60,90,120,146]:items.append(dict(name=f'seed{seed}-turn{turn}',walls=list(w),state=list(s)))
            r=run(lib,s);lib.step(s,Move(*r['action'],0))
    (D/'positions.json').write_text(json.dumps(items,indent=2))
    print(json.dumps({'saved':len(items)}),flush=True)
def profile(tmp,args):
    libs=[build(D/'baseline.c',tmp,'baseline'),build(D/'instrumented.c',tmp,'instrumented'),build(D/'experiment.c',tmp,'experiment') if (D/'experiment.c').exists() else None]
    for p in json.loads((D/'positions.json').read_text()):
        for idx,lib in enumerate(libs):
            if lib is None:continue
            setup(lib,p['walls']);r=run(lib,p['state'],args.depth,idx==2,args.timing)
            print(json.dumps(dict(position=p['name'],variant=idx,**r)),flush=True)
def matches(tmp,args):
    libs=[build(D/'experiment.c',tmp,'old',profile=1,budget=args.budget),build(D/'experiment.c',tmp,'new',profile=1,budget=args.budget)]
    for seed in range(args.seed,args.seed+args.pairs):
        for seat in (seed%2,1-seed%2):
            s,w=fixture(seed)
            for lib in libs:setup(lib,w)
            stats=[[],[]]
            while not libs[0].ended(s):
                k=int(s[56]==seat);r=run(libs[k],s,mode=k,observe=1)
                r['metrics']={key:r['metrics'][key] for key in ['reductions','researches','research_nodes','pvs','pvs_nodes','aspiration','aspiration_nodes','tt_cut','qtt_cut']}
                stats[k].append(r)
                libs[0].step(s,Move(*r['action'],0))
            counts=[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in range(2)]
            print(json.dumps(dict(seed=seed,seat=seat,new=counts[seat],baseline=counts[1-seat],stats=stats)),flush=True)
def tactical(tmp,args):
    libs=[build(D/'baseline.c',tmp,'ref'),build(D/'experiment.c',tmp,'lmr')]
    for p in json.loads((D/'tactics.json').read_text()):
        results=[]
        for k,dep in [(0,args.depth+2),(0,args.depth),(1,args.depth)]:
            setup(libs[k],p['walls']);r=run(libs[k],p['state'],dep,k)
            # Score chosen action at identical deeper reference horizon, fresh caches.
            child=Array(*p['state']);libs[0].step(child,Move(*r['action'],0));setup(libs[0],p['walls'])
            r['reference_action_value']=-run(libs[0],child,args.depth+1)['value']
            results.append(r)
        print(json.dumps(dict(position=p['name'],results=results)),flush=True)
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('task',choices=['save','profile','matches','tactical']);ap.add_argument('--depth',type=int,default=5);ap.add_argument('--timing',type=int,default=0);ap.add_argument('--budget',type=int,default=49);ap.add_argument('--seed',type=int,default=600);ap.add_argument('--pairs',type=int,default=12);args=ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp)
        if args.task=='save':save_positions(tmp)
        elif args.task=='profile':profile(tmp,args)
        elif args.task=='matches':matches(tmp,args)
        else:tactical(tmp,args)
