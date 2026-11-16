#define BOT_TEST
#include "/Users/hexagone/Documents/ChatGPT/cultist-wars/validation/component-tweaks-41714601/guard_futility.c"
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();}
int run(const int *p,int depth,int *out,uint64_t *stats){
State s=unpack(p);uint64_t start=now_ns();turn_deadline=start+(uint64_t)BUDGET_MS*1000000ULL;
Action a={WAIT,0,0,0};int value,completed=depth;
if(depth){prepare(&s);nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;deadline=start+30000000000ULL;value=search(&s,depth,-INF,INF,&a);}
else a=choose(&s,&completed,&value);
memcpy(out,&a,sizeof a);stats[0]=completed;stats[1]=nodes;stats[2]=qnodes;stats[3]=now_ns()-start;
return value;}
int score_child(const int *p,const int *act,int depth){State s=unpack(p);prepare(&s);State c=apply(&s,*(const Action*)act);
uint64_t start=now_ns();deadline=start+30000000000ULL;turn_deadline=deadline;stopped=0;Action a;int v;
if(terminal(&c,&v))return -v; prepare(&c);return -search(&c,depth,-INF,INF,&a);}

void step(int *p,const int *a){State s=unpack(p);s=apply(&s,*(const Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
int static_eval(const int *p){State s=unpack(p);return evaluate(&s);}

void configure(int f){use_futility=f;}
void init_cost(const int*p,uint64_t*out){State s=unpack(p);prepare(&s);uint64_t a=now_ns();nn_begin(&s);*out=now_ns()-a;nn_root=0;}
