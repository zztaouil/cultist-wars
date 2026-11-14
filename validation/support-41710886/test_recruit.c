#define BOT_TEST
#include "recruit-q.c"
#include <assert.h>
int main(void) {
    initialize();
    State s={0};s.remaining=80;
    s.u[0]=(Unit){0,10,0,1};
    s.u[1]=(Unit){3,10,1,1};
    s.u[2]=(Unit){2,10,2,0};prepare(&s);
    assert(recruitment_threat(&s));
    s.u[0].pos=26;prepare(&s);assert(!recruitment_threat(&s));
    s.u[0].pos=0;s.u[1].pos=4;prepare(&s);assert(!recruitment_threat(&s));
    s.u[1].pos=3;s.u[2].owner=1;prepare(&s);assert(!recruitment_threat(&s));
    s.u[2].owner=2;s.u[0].hp=0;prepare(&s);assert(!recruitment_threat(&s));
    s.u[0].hp=10;wall[1]=1;initialize();prepare(&s);
    assert(!recruitment_threat(&s)); /* Wall detour exceeds contest radius. */
    memset(wall,0,sizeof wall);initialize();prepare(&s);
    assert(recruitment_threat(&s));
    /* Player symmetry: swap sides and ownership together. */
    s.side=1;s.u[0].owner=1;s.u[1].owner=0;prepare(&s);
    assert(recruitment_threat(&s));
    puts("PASS: contested recruitment, distance, walls, ownership, dead leader, symmetry");
}
