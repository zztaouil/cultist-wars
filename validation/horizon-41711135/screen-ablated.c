
#define BOT_TEST
#include "/Users/hexagone/Documents/ChatGPT/cultist-wars/validation/horizon-41711135/combined.c"
static Tracker tr;static int first;static State obs;
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tr,0,sizeof tr);first=1;}
int observe(int *p){obs=unpack(p);obs.rng=0;obs.rng_known=0;obs.neutral_mask=0;track_observation(&tr,&obs,first);first=0;p[58]=obs.rng;p[59]=obs.neutral_mask;p[60]=obs.rng_known;return tr.n;}
int history_size(void){return sizeof history;}
void history_get(void *p){memcpy(p,history,sizeof history);}
void history_set(void *p){memcpy(history,p,sizeof history);memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(eval_tt,0,sizeof eval_tt);}
void actual(int *a){track_action(&tr,*(Action*)a);}
int probe(int *p,int depth,int ms,int *out,uint64_t *st){State s=unpack(p);prepare(&s);uint64_t start=now_ns();turn_deadline=start+(uint64_t)ms*1000000ULL;deadline=turn_deadline;nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;Action a={0};int v,d=depth;
if(depth){nn_begin(&s);v=search(&s,depth,-INF,INF,&a);nn_root=0;}else a=choose(&s,&d,&v);
memcpy(out,&a,sizeof a);st[0]=d;st[1]=nodes;st[2]=qnodes;st[3]=now_ns()-start;st[4]=stopped;return v;}
