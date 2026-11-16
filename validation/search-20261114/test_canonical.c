#define BOT_TEST
#include "canonical.c"
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

    }
    State s={0}; s.remaining=100; s.rng=42; s.rng_known=1;
    s.u[0]=(Unit){39,10,0,1}; s.u[1]=(Unit){51,10,1,1}; prepare(&s);
    State other=s; other.rng=12345; other.neutral_mask=4; other.remaining=80; prepare(&other);
    assert(s.key==other.key);
    other.remaining=36; prepare(&other); assert(s.key!=other.key);
    other=s; other.remaining=1; prepare(&other); assert(s.key!=other.key);
    other=s; other.u[2]=(Unit){40,10,2,0}; other.neutral_mask=4; prepare(&other);
    State third=other; third.rng++; prepare(&third); assert(third.key!=other.key);
    third=other; third.remaining--; prepare(&third); assert(third.key!=other.key);
    /* Canonical search identities do not alter referee RNG or unit transitions. */
    State a=apply(&s,(Action){WAIT,0,0,0});
    other=s; other.neutral_mask=4; prepare(&other);
    State b=apply(&other,(Action){WAIT,0,0,0});
    assert(a.rng!=b.rng && a.key==b.key);
    puts("PASS: Q bounds, horizon isolation, irrelevant RNG canonicalization");
}
