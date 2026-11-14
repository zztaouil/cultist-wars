
#define BOT_TEST
#include "baseline.c"
static State unpack(int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();use_futility=0;use_threat_q=1;}
void fresh(void){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(history,0,sizeof history);nn_root=0;nodes=qnodes=0;stopped=0;deadline=now_ns()+5000000000ULL;turn_deadline=deadline;}
int solve(int *p,int depth,int qleft,int *out,int *ok){State s=unpack(p);fresh();Action a={-1};int v;
if(depth)v=search(&s,depth,-INF,INF,&a);
else {v=quiescence(&s,-INF,INF,qleft);int tv;
if(qleft&&!terminal(&s,&tv)) {int unstable=s.remaining<=qleft||material_threat(&s);int best=unstable?-INF:evaluate_live(&s);Action aa[ACTIONS];int n=generate_mode(&s,aa,!unstable);for(int i=0;i<n;i++){State c=apply(&s,aa[i]);int z=-quiescence(&c,-INF,INF,qleft-1);if(z>best){best=z;a=aa[i];}}}}
memcpy(out,&a,sizeof a);*ok=!stopped;return v;}
void step(int *p,int *a){State s=unpack(p);s=apply(&s,*(Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
