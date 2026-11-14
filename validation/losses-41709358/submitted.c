#define BOT_TEST
#include "/Users/hexagone/Documents/ChatGPT/cultist-wars/submissions/41709358.c"
static Tracker tracker;static int turns;
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tracker,0,sizeof tracker);turns=0;}
int run(const int *p,int depth,int mode,int timing,int observe,int *out,uint64_t *stats){
State s=unpack(p);
uint64_t start=now_ns();turn_deadline=start+(uint64_t)BUDGET_MS*1000000ULL;
if(observe)track_observation(&tracker,&s,turns++==0);
Action a={WAIT,0,0,0};int value,completed=depth;
if(depth){prepare(&s);nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;deadline=start+30000000000ULL;value=search(&s,depth,-INF,INF,&a);}
else a=choose(&s,&completed,&value);
if(observe)track_action(&tracker,a);
memcpy(out,&a,sizeof a);stats[0]=completed;stats[1]=nodes;stats[2]=qnodes;stats[3]=now_ns()-start;stats[4]=stopped;
return value;}
void step(int *p,const int *a){State s=unpack(p);s=apply(&s,*(const Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
int ended(const int *p){State s=unpack(p);int v;return terminal(&s,&v);}
