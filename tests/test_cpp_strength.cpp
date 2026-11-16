#define BOT_TEST
#include "../bot.cpp"
#include <cassert>
int main() {
    initialize();
    State s{}; s.remaining=100;
    s.u[0]=Unit{39,10,0,1}; s.u[1]=Unit{51,10,1,1};
    s.u[2]=Unit{41,10,2,0}; prepare(&s);
    int access=recruit_control(&s);
    assert(access>0); // Nearby live leader retains recruitment options.
    int v=evaluate_live(&s);
    for(int i=0;i<UNITS;i++)if(s.u[i].hp && s.u[i].owner<2)s.u[i].owner^=1;
    prepare(&s);assert(recruit_control(&s)==-access);assert(evaluate_live(&s)==-v);
    s.u[2].hp=0;prepare(&s);assert(recruit_control(&s)==0);
    s={};s.remaining=100;
    s.u[0]=Unit{39,10,0,1};s.u[1]=Unit{51,10,1,1};
    s.u[2]=Unit{42,5,0,0};s.u[3]=Unit{45,10,1,0};prepare(&s);
    int nonlethal=safety_value(&s);
    s.u[2].hp=4;prepare(&s);
    assert(safety_value(&s)==nonlethal-LETHAL_PENALTY);
    // Leader exposure premium disappears when recruitment is exhausted.
    s={};s.remaining=100;
    s.u[0]=Unit{42,10,0,1};s.u[1]=Unit{51,10,1,1};
    s.u[2]=Unit{45,10,1,0};prepare(&s);
    int exhausted=safety_value(&s);
    s.u[3]=Unit{0,10,2,0};prepare(&s);
    assert(safety_value(&s)==exhausted-4*LEADER_EXPOSURE);
    for(int i=0;i<UNITS;i++)if(s.u[i].hp && s.u[i].owner<2)s.u[i].owner^=1;
    prepare(&s);assert(safety_value(&s)==-exhausted+4*LEADER_EXPOSURE);
    // Equal access to a contested neutral remains symmetric.
    s={};s.remaining=100;
    s.u[0]=Unit{40,10,0,1};s.u[1]=Unit{42,10,1,1};
    s.u[2]=Unit{41,10,2,0};prepare(&s);assert(recruit_control(&s)==0);
    s.u[0].pos=39;prepare(&s);
    int distant=RECRUIT_WEIGHT/3;
    if(RECAPTURE_DISCOUNT)distant/=2;
    assert(recruit_control(&s)==distant-RECRUIT_WEIGHT/2-RACE_WEIGHT/2);
    // Exhausted budget still returns a legal policy-ranked fallback.
    turn_deadline=1;int depth,value;Action selected=choose(&s,&depth,&value);
    Action legal[ACTIONS];int n=generate(&s,legal),found=0;
    for(int i=0;i<n;i++)found|=same(selected,legal[i]);
    assert(found && depth==0);
    puts("PASS: C++ recruitment symmetry, lethal exposure, deadline fallback");
}
