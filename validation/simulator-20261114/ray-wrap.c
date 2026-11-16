
#define BOT_TEST
#include "/Users/hexagone/Documents/ChatGPT/cultist-wars/validation/simulator-20261114/ray.c"
#include <assert.h>
static volatile uint64_t sink;
static State unpack(const int*p){State s={0};for(int i=0;i<14;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
static void pack(State s,int*p){for(int i=0;i<14;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;p[61]=0;}
static void check(State s){State f=s;prepare(&f);assert(s.key==hash_state(&s));assert(s.key==f.key&&s.metadata==f.metadata&&s.leaders==f.leaders);assert(!memcmp(s.owned,f.owned,sizeof s.owned));assert(!memcmp(s.occupants,f.occupants,sizeof s.occupants));assert(!memcmp(&s.occupied,&f.occupied,sizeof s.occupied));}
void setup(const int*w){memcpy(wall,w,sizeof wall);initialize();nn_init();}
int solve(const int*p,int depth,int*out,uint64_t*stats){State s=unpack(p);Action a={0};int v,done=depth;nodes=qnodes=hits=cutoffs=pruned=0;stopped=0;turn_deadline=0;deadline=now_ns()+60000000000ULL;uint64_t start=now_ns();if(depth){nn_begin(&s);v=search(&s,depth,-INF,INF,&a);nn_root=0;}else a=choose(&s,&done,&v);stats[0]=now_ns()-start;stats[1]=nodes;stats[2]=qnodes;stats[3]=hits;stats[4]=cutoffs;stats[5]=pruned;stats[6]=stopped;stats[7]=done;out[0]=a.kind;out[1]=a.unit;out[2]=a.target;return v;}
int actions(const int*p,int*out){State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);for(int i=0;i<n;i++){out[4*i]=a[i].kind;out[4*i+1]=a[i].unit;out[4*i+2]=a[i].target;out[4*i+3]=a[i].order;}return n;}
void transition(const int*p,int index,int prepared,int*out){State s=unpack(p);if(!prepared)s.ready=0;Action a[ACTIONS];generate(&s,a);State t=apply(&s,a[index]);if(prepared)check(t);pack(t,out);}
void micro(const int*p,int reps,uint64_t*out){State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);uint64_t start=now_ns(),sum=0;for(int k=0;k<reps;k++)for(int i=0;i<n;i++){State t=apply(&s,a[i]);sum+=t.key+t.rng;}sink=sum;out[0]=now_ns()-start;out[1]=(uint64_t)reps*n;out[2]=sizeof(State);out[3]=sizeof(Action);out[4]=sizeof(Entry);}

