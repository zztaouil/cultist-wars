#define BOT_TEST
#include "experiment.c"
#include <assert.h>
static uint64_t probe(State s,int depth,int pv) {
    memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(history,0,sizeof history);
    memset(&metrics,0,sizeof metrics);nodes=qnodes=0;stopped=0;
    use_lmr=1;use_futility=0;deadline=now_ns()+10000000000ULL;
    prepare(&s);Action a;search(&s,depth,pv?-INF:100000,pv?INF:100001,&a);
    assert(!stopped);return metrics.reductions;
}
int main(void) {
    initialize();State s={0};s.remaining=100;
    s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};
    s.u[2]=(Unit){27,10,0,0};s.u[3]=(Unit){62,10,1,0};
    assert(probe(s,3,0)>0); /* exercise reductions, not a vacuous test */
    assert(probe(s,3,1)==0); /* PV root; descendants too shallow */
    assert(probe(s,2,0)==0);
    s.remaining=7;assert(probe(s,3,0)==0);s.remaining=100;
    s.u[2].leader=s.u[3].leader=1;assert(probe(s,3,0)==0);
    s.u[2].leader=s.u[3].leader=0;
    s.u[1].pos=28;prepare(&s);assert(material_threat(&s));
    assert(probe(s,3,0)==0); /* immediate conversion evasion */
    s=(State){0};s.remaining=100;s.side=1;
    s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};
    s.u[2]=(Unit){40,10,2,0};prepare(&s);assert(lmr_creates_threat(&s));
    s.u[2].owner=1;prepare(&s);assert(lmr_creates_threat(&s));
    s=(State){0};s.side=0;s.remaining=30;wall[1]=1;initialize();
    s.u[0]=(Unit){0,10,1,0};s.u[1]=(Unit){15,4,0,0};s.u[2]=(Unit){29,10,2,0};prepare(&s);
    assert(lmr_creates_threat(&s)); /* indirect lethal shot */
    puts("PASS: LMR exercised; PV, depth, horizon, leaders, evasion, recruitment, indirect threats");
}
