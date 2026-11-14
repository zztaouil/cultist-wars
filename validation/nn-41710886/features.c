#define BOT_TEST
#include "baseline.c"
#define NF 128
static State unpack(const int *p) {
 State s={0}; for(int i=0;i<14;i++) s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};
 s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;
}
void setup(const int *w){memcpy(wall,w,sizeof wall);initialize();}
static int victim(const State*s,Action a){return a.kind==SHOOT?fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos):-1;}
/* Fixed scaling; no fitted statistics. Unit slots retain referee IDs. */
static void features(const State*s,Action a,float*f){
 memset(f,0,NF*sizeof(float));int src=a.kind?s->u[a.unit].pos:45;
 int hit=victim(s,a);int dst=a.kind==MOVE?a.target: a.kind==CONVERT?s->u[a.target].pos:hit>=0?s->u[hit].pos:src;
 int k=0;for(int i=0;i<14;i++){Unit u=s->u[i];if(u.hp){f[k]=(u.pos%13-dst%13)/12.f;f[k+1]=(u.pos/13-dst/13)/6.f;f[k+2]=u.hp/10.f;f[k+3]=u.owner==s->side;f[k+4]=u.owner==1-s->side;f[k+5]=u.owner==2;f[k+6]=u.leader;}k+=7;}
 f[98+a.kind]=1;f[102]=src%13/12.f;f[103]=src/13/6.f;f[104]=dst%13/12.f;f[105]=dst/13/6.f;
 f[106]=a.kind?s->u[a.unit].hp/10.f:0;f[107]=a.kind?s->u[a.unit].leader:0;
 f[108]=s->remaining/150.f;f[109]=a.order/4000.f;
 if(hit>=0){f[110]=(7-taxi[src][dst])/7.f;f[111]=(7-taxi[src][dst])>=s->u[hit].hp;f[112]=s->u[hit].owner==s->side;f[113]=s->u[hit].owner==2;}
 for(int group=0;group<3;group++){int owner=group==0?s->side:group==1?1-s->side:2;int count=0,hp=0,near=91,leader=91;
 for(int i=0;i<14;i++)if(s->u[i].hp&&s->u[i].owner==owner){count++;hp+=s->u[i].hp;int d=distance_table[dst][s->u[i].pos];if(i!=a.unit||!a.kind){if(d<near)near=d;}if(s->u[i].leader&&d<leader)leader=d;}
 f[114+group*4]=count/14.f;f[115+group*4]=hp/140.f;f[116+group*4]=near/91.f;f[117+group*4]=leader/91.f;}
 f[126]=distance_table[src][dst]/91.f;f[127]=s->side;
}
int extract(const int*p,const int*expert,float*out,int*orders,int*label){State s=unpack(p);Action aa[ACTIONS],e={expert[0],expert[1],expert[2],0};int n=generate(&s,aa);*label=-1;
 for(int i=0;i<n;i++){features(&s,aa[i],out+i*NF);orders[i]=aa[i].order;if(same(aa[i],e)||(e.kind==SHOOT&&aa[i].kind==SHOOT&&aa[i].unit==e.unit&&victim(&s,aa[i])==victim(&s,e)))*label=i;}return n;}
int verify_step(const int*p,const int*a,const int*q){State s=unpack(p);State t=apply(&s,(Action){a[0],a[1],a[2],0});
 for(int i=0;i<14;i++){if(t.u[i].hp!=q[4*i+1]||t.u[i].owner!=q[4*i+2]||t.u[i].leader!=q[4*i+3]||(t.u[i].hp&&t.u[i].pos!=q[4*i]))return 0;}
 return t.side==q[56]&&t.remaining==q[57]&&t.rng==(uint32_t)q[58]&&t.neutral_mask==q[59];}
