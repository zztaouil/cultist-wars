#define BOT_TEST
#include "fast.c"
#include <assert.h>
int main(void) {
    initialize();
    for(int h=1;h<=10;h++)for(int r=0;r<8;r++)for(int d=0;d<CELLS;d++)
        assert(recruit_value[h][r][d]==RECRUIT_WEIGHT*(2*h-(r>h?h:r))/(2*h*(d+2)));
    State s={0};s.remaining=100;
    s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};
    s.u[2]=(Unit){42,10,2,0};prepare(&s);
    assert(recruit_distance(&s,0,2)==2);
    assert(recruit_distance(&s,-1,2)==CELLS);
    int before=recruit_control(&s);assert(before>0);
    s.u[3]=(Unit){40,10,0,0};prepare(&s);
    assert(recruit_distance(&s,0,2)==4);
    assert(recruit_control(&s)<before);
    s.u[3].hp=0;s.u[0].pos=41;prepare(&s);
    assert(recruit_distance(&s,0,2)==0);
    before=recruit_control(&s);
    s.u[3]=(Unit){44,10,1,0};prepare(&s);
    assert(recruit_control(&s)<before);
    int v=recruit_control(&s);
    for(int i=0;i<UNITS;i++) if(s.u[i].hp && s.u[i].owner<2)s.u[i].owner^=1;
    prepare(&s);assert(recruit_control(&s)==-v);
    int cached=evaluate_live(&s);s.side=1;prepare(&s);
    assert(evaluate_live(&s)==-cached);
    s.u[2].hp=0;prepare(&s);assert(recruit_control(&s)==0);
    s.u[2]=(Unit){42,1,2,0};s.u[0].hp=s.u[1].hp=0;prepare(&s);
    assert(recruit_control(&s)==0);
    s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};
    for(int y=0;y<H;y++)wall[y*W+2]=1;
    initialize();prepare(&s);assert(recruit_distance(&s,0,2)==CELLS);
    puts("PASS: recruitment access, blocking, exposure, symmetry, cache, dead leaders, walls");
}
