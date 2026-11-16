#ifndef RECRUIT_WEIGHT
#define RECRUIT_WEIGHT 120
#endif
/* Arrival estimate: require a free first step, then use obstacle distances.
   It is a heuristic, not a prediction of future occupancy or neutral RNG. */
static int recruit_distance(const State *s, int leader, int target) {
    if (leader < 0) return CELLS;
    int p=s->u[leader].pos, q=s->u[target].pos;
    if (taxi[p][q]==1) return 0;
    int best=CELLS;
    for(int k=0;k<4;k++) {
        int n=adjacent[p][k];
        if(n<0 || wall[n] || bit_has(s->occupied,n)) continue;
        int d=distance_table[n][q];
        if(d<best) best=d;
    }
    return best;
}
/* Discount access when the prospective recruit can be shot immediately.
   Max damage, not a sum: an opponent can fire only one unit per action. */
static int recruit_control(const State *s) {
    int leader[2]={-1,-1},value=0;
    unsigned leaders=s->leaders;
    while(leaders) {int i=__builtin_ctz(leaders);leaders&=leaders-1;
        if(s->u[i].owner<2) leader[s->u[i].owner]=i;}
    unsigned ns=s->owned[2];
    while(ns) {
        int i=__builtin_ctz(ns);ns&=ns-1;
        int d[2]={recruit_distance(s,leader[0],i),recruit_distance(s,leader[1],i)};
        int access[2]={0,0};
        for(int p=0;p<2;p++) {
            if(d[p]>=CELLS) continue;
            int risk=0;
            unsigned enemies=s->owned[1-p]&~s->leaders;
            while(enemies) {int j=__builtin_ctz(enemies);enemies&=enemies-1;
                int damage=7-taxi[s->u[j].pos][s->u[i].pos];
                if(damage>risk && fast_hit(s,s->u[j].pos,s->u[i].pos)==i) risk=damage;}
            if(risk>s->u[i].hp) risk=s->u[i].hp;
            access[p]=RECRUIT_WEIGHT*(2*s->u[i].hp-risk)/(2*s->u[i].hp*(d[p]+2));
        }
        value+=access[0]-access[1];
    }
    return value;
}
