#define BOT_TEST
#include "candidate.c"
#include <assert.h>
int main(void) {
    initialize(); use_futility = 0;
    assert(sizeof(QEntry) == 16);
    for (int k=0;k<30;k++) {
        State s={0}; s.remaining=20; s.side=k%2;
        s.u[0]=(Unit){39,10,0,1}; s.u[1]=(Unit){51,10,1,1};
        s.u[2]=(Unit){40+k%3,2+k%8,0,0};
        s.u[3]=(Unit){44+k%3,1+k%9,1,0};
        s.u[4]=(Unit){27,10,2,0}; prepare(&s);
        deadline=now_ns()+10000000000ULL; stopped=0;
        memset(qtt,0,sizeof qtt);
        int exact=quiescence(&s,-INF,INF,4);
        for(int upper=0;upper<2;upper++) {
            memset(qtt,0,sizeof qtt);
            /* A valid non-cutting bound must tighten, never change the value. */
            qtt[s.key&(TT_SIZE-1)]=(QEntry){s.key,exact+(upper?10:-10),5,upper?2:1,0};
            assert(quiescence(&s,-INF,INF,4)==exact);
            /* A stale horizon may order moves but cannot supply a bound. */
            memset(qtt,0,sizeof qtt);
            qtt[s.key&(TT_SIZE-1)]=(QEntry){s.key,WIN,3,0,1};
            assert(quiescence(&s,-INF,INF,4)==exact);
        }
        memset(tt,0,sizeof tt); memset(qtt,0,sizeof qtt);
        search_root=&s; partial_valid=0;
        Action a; int value=search(&s,3,-INF,INF,&a);
        assert(partial_valid && same(a,partial_action) && value==partial_value);
        memset(tt,0,sizeof tt); memset(qtt,0,sizeof qtt); partial_valid=0;
        search(&s,3,value+1,value+2,&a);
        assert(!partial_valid); /* fail-low scores are not exact improvements */
        search_root=0;
    }
    puts("PASS: Q bounds/horizons, root progress, fail-low rejection");
}
