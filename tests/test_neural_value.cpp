#define BOT_TEST
#include "../bot.cpp"
#include <cassert>
int main() {
    initialize();State s{};s.remaining=100;s.rng_known=1;s.rng=42;
    s.u[0]=Unit{39,10,0,1};s.u[1]=Unit{51,10,1,1};
    s.u[2]=Unit{41,10,2,0};s.neutral_mask=4;prepare(&s);
    float x[VALUE_FEATURES],y[VALUE_FEATURES];value_features(&s,x);
    assert(x[91+16+5]==1 && x[91+16+7]==1 && x[209]==1);
    int expected=(int)lroundf(10000*value_forward(x));assert(evaluate_live(&s)==expected);
    // Metadata must not alias the old board-only evaluation cache.
    for(int side=0;side<2;++side)for(int remaining=1;remaining<150;remaining+=17) {
        s.side=side;s.remaining=remaining;s.rng+=199;prepare(&s);value_features(&s,x);
        assert(evaluate_live(&s)==(int)lroundf(10000*value_forward(x)));
        assert(abs(evaluate_live(&s))<=10000);
    }
    s.rng_known=0;value_features(&s,x);s.rng=99999;value_features(&s,y);
    assert(memcmp(x,y,sizeof x)==0);for(int i=208;i<239;++i)assert(x[i]==0);
    s.u[2].hp=0;value_features(&s,x);assert(x[114]==1); // Dead neutral list entry retained.
    s.remaining=0;s.side=0;s.u[2]=Unit{41,1,0,0};prepare(&s);
    assert(evaluate(&s)==WIN);s.side=1;prepare(&s);assert(evaluate(&s)==-WIN);
    s.remaining=100;prepare(&s);turn_deadline=1;int depth,value;
    Action chosen=choose(&s,&depth,&value),legal[ACTIONS];int n=generate(&s,legal),found=0;
    for(int i=0;i<n;++i)found|=same(chosen,legal[i]);assert(found && depth==0);
    puts("PASS: neural leaf, metadata cache, RNG masking, terminal override, legal fallback");
}
