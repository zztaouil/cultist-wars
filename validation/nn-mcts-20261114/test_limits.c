#define BOT_TEST
#define MCTS_NODES 4
#define MCTS_EDGES 300
#include "candidate.c"
#include <assert.h>
int main(void){
 initialize();State s={0};s.side=0;s.remaining=150;s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};s.u[2]=(Unit){40,10,2,0};prepare(&s);
 int d,v;turn_deadline=now_ns()-1;Action a=choose(&s,&d,&v);
 assert(mc_n==0&&mc_simulations==0);
 Action legal[ACTIONS];int n=generate(&s,legal),ok=0;for(int i=0;i<n;i++)ok|=same(a,legal[i]);assert(ok);
 turn_deadline=now_ns()+1000000000ULL;a=choose(&s,&d,&v);assert(mc_full&&mc_n<=MCTS_NODES&&mc_e<=MCTS_EDGES);
 ok=0;for(int i=0;i<n;i++)ok|=same(a,legal[i]);assert(ok);
 puts("PASS: expired-deadline fallback; bounded arena saturation; legal output");
}
