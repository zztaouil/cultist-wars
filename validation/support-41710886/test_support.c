#define BOT_TEST
#include "candidate.c"
#include <assert.h>
int main(void) {
    initialize();
    State s = {0}; s.remaining = 100;
    s.u[0] = (Unit){0,10,0,0};
    s.u[1] = (Unit){2,10,1,0};
    prepare(&s);
    assert(fire_support(&s,0)==5);
    /* Friendly/neutral blockers must not earn hostile pressure. */
    s.u[2] = (Unit){1,10,2,0}; prepare(&s);
    assert(fire_support(&s,0)==0);
    s.u[2].owner=0; s.u[2].leader=1; prepare(&s);
    assert(fire_support(&s,0)==0);
    /* Indirect aim sees a victim hidden behind a wall on its direct ray. */
    memset(wall,0,sizeof wall); wall[1]=1; initialize();
    s=(State){0}; s.remaining=100;
    s.u[0]=(Unit){0,10,0,0}; s.u[1]=(Unit){15,4,1,0};
    s.u[2]=(Unit){29,10,2,0}; prepare(&s);
    assert(fast_hit(&s,0,15)==-1 && fast_hit(&s,0,29)==1);
    assert(fire_support(&s,0)==4);
    /* Swapping ownership negates static score, independent of side cache reuse. */
    int value=evaluate(&s);
    for(int i=0;i<UNITS;i++) if(s.u[i].hp && s.u[i].owner<2) s.u[i].owner^=1;
    prepare(&s); assert(evaluate(&s)==-value);
    s.side=1; prepare(&s); assert(evaluate(&s)==value);
    s.remaining=0; prepare(&s); assert(evaluate(&s)==0);
    puts("PASS: blockers, indirect pressure, ownership symmetry, cache, terminal draw");
}
