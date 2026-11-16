#define main unused_main
#include "WRAPPER"
#undef main
static unsigned seed=918273;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){
for(int board=0;board<8;board++){
for(int p=0;p<CELLS;p++)wall[p]=(rnd()%9==0);initialize();
for(int trial=0;trial<16;trial++){
State s={0};s.remaining=150;s.rng=42;s.rng_known=1;
for(int i=0;i<14;i++){int p;do{p=rnd()%CELLS;}while(wall[p]||bit_has(s.occupied,p));s.u[i]=(Unit){p,1+rnd()%10,i<2?i:rnd()%3,i<2};bit_toggle(&s.occupied,p);if(s.u[i].owner==2)s.neutral_mask|=1u<<i;}
prepare(&s);int occ[CELLS];occupancy(&s,occ);
for(int a=0;a<CELLS;a++)for(int b=0;b<CELLS;b++)assert(fast_hit(&s,a,b)==bullet_hit(a,b,occ));
Action actions[ACTIONS];int n=generate(&s,actions);
for(int i=0;i<n;i++){State t=apply(&s,actions[i]);check(t);
UNDO_CHECK
META_CHECK
}
}
}
/* Every membership mask, each possible rank: includes dead entries and absent ranks. */
memset(wall,0,sizeof wall);initialize();
for(unsigned mask=0;mask<(1u<<14);mask++)for(unsigned rank=0;rank<12;rank++){
State s={0};s.remaining=150;s.rng_known=1;s.neutral_mask=mask;
for(int i=0;i<14;i++)s.u[i]=(Unit){i*3,(i%3)?10:0,2,0};
for(unsigned r=0;;r++)if(((r*1103515245u+12345u)&2147483647u)%12==rank){s.rng=r;break;}
prepare(&s);State t=apply(&s,(Action){WAIT,0,0,0});check(t);
unsigned expected=(s.rng*1103515245u+12345u)&2147483647u;int id=-1;unsigned k=rank;
if(!mask)expected=s.rng;
else for(int i=0;i<14;i++)if(mask&(1u<<i)){if(!k--){id=i;break;}}
if(id>=0&&s.u[id].hp)expected=(expected*1103515245u+12345u)&2147483647u;
assert(t.rng==expected);
}
/* Trapped live neutral still consumes direction draw; dead one does not. */
State s={0};s.u[2]=(Unit){40,10,2,0};s.neutral_mask=4;s.rng_known=1;s.remaining=150;
for(unsigned r=0;;r++)if(((r*1103515245u+12345u)&2147483647u)%12==0){s.rng=r;break;}
for(int d=0;d<4;d++)wall[neighbor(40,d)]=1;initialize();prepare(&s);State t=apply(&s,(Action){WAIT,0,0,0});assert(t.u[2].pos==40);unsigned r=(s.rng*1103515245u+12345u)&2147483647u;r=(r*1103515245u+12345u)&2147483647u;assert(t.rng==r);
/* Corpse blocks the sole exit until after the neutral turn. */
memset(wall,0,sizeof wall);wall[27]=wall[39]=wall[53]=1;initialize();memset(&s,0,sizeof s);
s.u[2]=(Unit){41,1,2,0};s.u[3]=(Unit){40,10,2,0};s.u[4]=(Unit){44,10,0,0};s.neutral_mask=12;s.rng_known=1;s.remaining=150;
for(unsigned r=0;;r++)if(((r*1103515245u+12345u)&2147483647u)%12==1){s.rng=r;break;}
prepare(&s);t=apply(&s,(Action){SHOOT,4,2,0});assert(!t.u[2].hp&&t.u[3].pos==40);check(t);
TRACKER_CHECK
printf("targeted rays, membership/dead/absent/trapped draws, caches: PASS\n");return 0;}
