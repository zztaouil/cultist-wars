"""Compare optimized engine against saved pre-optimization implementation."""
import ctypes as C
import json
from pathlib import Path
import random
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
Int = C.c_int
State = Int * 61
Actions = Int * (253 * 4)


def build(source, directory, name, optimized, defines=()):
    code = r'''
#define BOT_TEST
#include "SOURCE"
static State unpack(const int *p) {
    State s={0};
    for(int i=0;i<UNITS;i++) s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};
    s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];
    PREPARE
    return s;
}
void setup(const int *w) {memcpy(wall,w,sizeof wall);initialize();}
int inspect(const int *p,int *actions) {
    State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);
    memcpy(actions,a,n*sizeof(Action));return n;
}
int score(const int *p) {State s=unpack(p);return evaluate(&s);}
void step(const int *p,const int *a,int *out) {
    State s=unpack(p);State t=apply(&s,(Action){a[0],a[1],a[2],a[3]});
    for(int i=0;i<UNITS;i++){out[4*i]=t.u[i].pos;out[4*i+1]=t.u[i].hp;out[4*i+2]=t.u[i].owner;out[4*i+3]=t.u[i].leader;}
    out[56]=t.side;out[57]=t.remaining;out[58]=t.rng;out[59]=t.neutral_mask;out[60]=t.rng_known;
    CHECK
}
int solve(const int *p,int depth,unsigned long long *stats) {
    State s=unpack(p);Action a;memset(tt,0,sizeof tt);nodes=qnodes=0;stopped=0;use_futility=0; DISABLE_NEW_LEAVES
    deadline=now_ns()+60000000000ULL;
    int v=search(&s,depth,-INF,INF,&a);stats[0]=nodes;stats[1]=qnodes;return v;
}
'''.replace('SOURCE', str(source)).replace('DISABLE_NEW_LEAVES', 'use_threat_q=0;' if optimized else '').replace('PREPARE', 'prepare(&s);' if optimized else '').replace('CHECK', '''
    if(t.key!=hash_state(&t)) abort();
    State fresh=t;prepare(&fresh);
    if(memcmp(&fresh.occupied,&t.occupied,sizeof(Bits)) || memcmp(fresh.owned,t.owned,sizeof t.owned) || fresh.leaders!=t.leaders || memcmp(fresh.occupants,t.occupants,sizeof t.occupants)) abort();
''' if optimized else '')
    path = directory / (name + '.c'); path.write_text(code)
    lib = directory / (name + '.so')
    subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',*defines,str(path),'-o',str(lib)],check=True)
    dll = C.CDLL(str(lib))
    return dll


with tempfile.TemporaryDirectory() as tmp:
    before = build(ROOT / 'submissions/41708923.c',Path(tmp),'before',False)
    # Model and search parity only: the leader-bonus weight was retuned to 0
    # after 41708923, so the reference leaf values need the historical weight.
    after = build(ROOT / 'validation/simulator-20261114/baseline.c',Path(tmp),'after',True,('-DLEADER_BONUS=420',))
    rng = random.Random(1928)
    fixtures = []
    transitions = 0
    for board in range(12):
        walls = (Int*91)(*[int(rng.random()<.13) for _ in range(91)])
        before.setup(walls);after.setup(walls)
        for trial in range(60):
            positions=rng.sample([p for p in range(91) if not walls[p]],14)
            state=State()
            for i,pos in enumerate(positions):
                state[4*i:4*i+4]=[pos,rng.randint(1,10),i if i<2 else rng.randrange(3),int(i<2)]
            state[56:61]=[rng.randrange(2),rng.randint(1,150),rng.randrange(1<<31),sum(1<<i for i in range(2,14) if state[4*i+2]==2),rng.randrange(2)]
            old,new=Actions(),Actions()
            n=before.inspect(state,old);m=after.inspect(state,new)
            assert m==n, (board,trial,n,m,list(state),[list(old[j*4:j*4+4]) for j in range(n)],[list(new[j*4:j*4+4]) for j in range(m)])
            assert list(old[:n*4])==list(new[:n*4])
            assert before.score(state)==after.score(state)
            for j in range(n):
                a=(Int*4)(*old[j*4:j*4+4]);x,y=State(),State()
                before.step(state,a,x);after.step(state,a,y)
                assert list(x)==list(y)
                transitions+=1
            if trial<2: fixtures.append((list(walls),list(state)))
    timing=[0.,0.]; visits=[0,0]
    for walls,state in fixtures:
        results=[]
        for k,dll in enumerate([before,after]):
            dll.setup((Int*91)(*walls)); stats=(C.c_ulonglong*2)()
            start=time.perf_counter();results.append(dll.solve(State(*state),4,stats))
            timing[k]+=time.perf_counter()-start;visits[k]+=stats[0]
        assert results[0]==results[1],results
    print(json.dumps({'positions':720,'transitions':transitions,'fixed_depth_positions':len(fixtures),'seconds_before_after':timing,'visits_before_after':visits,'speedup':timing[0]/timing[1]}))
