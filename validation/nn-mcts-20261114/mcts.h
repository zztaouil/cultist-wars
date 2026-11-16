#include <math.h>
#ifndef MCTS_NODES
#define MCTS_NODES 8192
#endif
#ifndef MCTS_EDGES
#define MCTS_EDGES 262144
#endif
#ifndef MCTS_SIMS
#define MCTS_SIMS 1000000
#endif
#ifndef MCTS_C
#define MCTS_C 1.5f
#endif
#ifndef MCTS_VALUE
#define MCTS_VALUE 1
#endif
#ifndef MCTS_SEARCH
#define MCTS_SEARCH 1
#endif
#ifndef MCTS_UNIFORM
#define MCTS_UNIFORM 0
#endif
#ifndef MCTS_CLOCK
#define MCTS_CLOCK 1
#endif
typedef struct { int first,count,visits; float value; } MCNode;
typedef struct { Action a; int child,visits; float prior,sum; } MCEdge;
static MCNode mc_nodes[MCTS_NODES];
static MCEdge mc_edges[MCTS_EDGES];
static int mc_n,mc_e,mc_calls,mc_simulations,mc_max_depth,mc_full;
static uint64_t mc_deadline;
static int mc_expired(void) { return MCTS_CLOCK && now_ns()>=mc_deadline; }
static float mc_terminal_value(int value) {return value>0?1.f:value<0?-1.f:0.f;}
static float mc_heuristic(const State *s) {return tanhf(evaluate(s)/2000.f);}
/* An incomplete expansion never enters the tree. */
static int mc_expand(const State *s) {
    if(mc_n==MCTS_NODES){mc_full=1;return -1;}
    int terminal_value;
    if(terminal(s,&terminal_value)) {
        int id=mc_n++;mc_nodes[id]=(MCNode){0,0,0,mc_terminal_value(terminal_value)};return id;
    }
    Action actions[ACTIONS];int n=generate(s,actions);
    if(mc_e+n>MCTS_EDGES){mc_full=1;return -1;}
    float priors[ACTIONS],maxlog=-1e30f,z[PV_Z];
    float value=pv_encode(s,z);mc_calls++;
    if(!MCTS_VALUE)value=mc_heuristic(s);
    for(int i=0;i<n;i++) {
        if(mc_expired())return -1;
        priors[i]=MCTS_UNIFORM?0:pv_logit(s,actions[i],z);
        if(priors[i]>maxlog)maxlog=priors[i];
    }
    float total=0;
    for(int i=0;i<n;i++){priors[i]=expf(priors[i]-maxlog);total+=priors[i];}
    if(mc_expired())return -1;
    int id=mc_n++;mc_nodes[id]=(MCNode){mc_e,n,0,value};
    for(int i=0;i<n;i++)mc_edges[mc_e++]=(MCEdge){actions[i],-1,0,priors[i]/total,0};
    return id;
}
static void mc_backup(const int *path, const int *parents, int length, int leaf, float value) {
    mc_nodes[leaf].visits++;
    for(int i=length-1;i>=0;i--){
        value=-value;MCEdge *edge=&mc_edges[path[i]];
        edge->visits++;edge->sum+=value;mc_nodes[parents[i]].visits++;
    }
}
static Action choose(const State *input,int *completed_depth,int *advantage) {
    if(!MCTS_SEARCH)return ab_choose(input,completed_depth,advantage);
    uint64_t start=now_ns();
    mc_deadline=turn_deadline?turn_deadline:start+(uint64_t)BUDGET_MS*1000000ULL;
    nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;
    mc_n=mc_e=mc_calls=mc_simulations=mc_max_depth=mc_full=0;
    State root=*input;prepare(&root);
    Action legal[ACTIONS];int count=generate(&root,legal);
    Action best=count?legal[0]:(Action){WAIT,0,0,0};
    *completed_depth=0;*advantage=0;
    int r=mc_expand(&root);if(r<0)return best;
    for(int sim=0;sim<MCTS_SIMS&&!mc_full&&!mc_expired();sim++) {
        State s=root;int id=0,len=0,path[151],parents[151],ok=1;
        while(mc_nodes[id].count) {
            if(mc_expired()||len>=150){ok=0;break;}
            MCNode *n=&mc_nodes[id];int selected=n->first;
            float best_score=-1e30f,explore=MCTS_C*sqrtf((float)(n->visits+1));
            for(int j=n->first;j<n->first+n->count;j++) {
                MCEdge *e=&mc_edges[j];
                float q=e->visits?e->sum/e->visits:n->value;
                float score=q+explore*e->prior/(1+e->visits);
                if(score>best_score){best_score=score;selected=j;}
            }
            path[len]=selected;parents[len++]=id;
            MCEdge *edge=&mc_edges[selected];s=apply(&s,edge->a);
            if(edge->child<0){
                int child=mc_expand(&s);if(child<0){ok=0;break;}
                edge->child=child;id=child;break;
            }
            id=edge->child;
        }
        if(!ok)break;
        mc_backup(path,parents,len,id,mc_nodes[id].value);
        mc_simulations++;if(len>mc_max_depth)mc_max_depth=len;
        if(!mc_nodes[0].count)break;
    }
    if(mc_nodes[0].count){
        int selected=mc_nodes[0].first;
        for(int j=selected+1;j<mc_nodes[0].first+mc_nodes[0].count;j++) {
            MCEdge *a=&mc_edges[j],*b=&mc_edges[selected];
            if(a->visits>b->visits||(a->visits==b->visits&&a->prior>b->prior))selected=j;
        }
        MCEdge *e=&mc_edges[selected];best=e->a;
        *advantage=(int)(10000*(e->visits?e->sum/e->visits:mc_nodes[0].value));
    }else *advantage=(int)(10000*mc_nodes[0].value);
    nodes=mc_simulations;*completed_depth=mc_max_depth;
    return best;
}
