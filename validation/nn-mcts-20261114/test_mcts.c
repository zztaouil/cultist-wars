#define BOT_TEST
#define MCTS_CLOCK 0
#define MCTS_SIMS 1024
#include "candidate.c"
#include <assert.h>
static void reset(void){memset(wall,0,sizeof wall);initialize();turn_deadline=0;}
static State position(int side){State s={0};s.side=side;s.remaining=1;
s.u[0]=(Unit){39,10,side,1};s.u[1]=(Unit){51,10,1-side,1};s.u[2]=(Unit){40,10,2,0};prepare(&s);return s;}
int main(void){
 reset();
 /* Explicit alternating-perspective backup, independent of learned weights. */
 mc_nodes[0]=(MCNode){0};mc_nodes[1]=(MCNode){0};mc_nodes[2]=(MCNode){0};
 mc_edges[0]=(MCEdge){0};mc_edges[1]=(MCEdge){0};
 int path[]={0,1},parents[]={0,1};mc_backup(path,parents,2,2,0.75f);
 assert(mc_edges[1].sum==-0.75f&&mc_edges[0].sum==0.75f);
 assert(mc_nodes[0].visits==1&&mc_nodes[1].visits==1&&mc_nodes[2].visits==1);
 for(int side=0;side<2;side++){
  reset();State s=position(side);int d,v;Action a=choose(&s,&d,&v);
  assert(a.kind==CONVERT&&a.unit==0&&a.target==2);assert(v==10000);
  assert(mc_nodes[0].visits==mc_simulations);
  float total=0;int visits=0;
  for(int j=0;j<mc_nodes[0].count;j++){total+=mc_edges[j].prior;visits+=mc_edges[j].visits;}
  assert(fabsf(total-1)<1e-5f&&visits==mc_simulations);
  int old=mc_n;assert(old>1);
  s.remaining=0;prepare(&s);a=choose(&s,&d,&v);assert(mc_n==1&&v==0);
  s.u[2].owner=side;prepare(&s);a=choose(&s,&d,&v);assert(v==10000);
  s.u[2].owner=1-side;prepare(&s);a=choose(&s,&d,&v);assert(v==-10000);
 }
 reset();State s=position(0);s.remaining=150;prepare(&s);
 mc_deadline=0;mc_n=MCTS_NODES;mc_full=0;assert(mc_expand(&s)==-1&&mc_full);
 mc_n=0;mc_e=MCTS_EDGES;mc_full=0;assert(mc_expand(&s)==-1&&mc_full);
 puts("PASS: backup perspective, both-seat forced win, visits, priors, terminal scoring, arena exhaustion");
}
