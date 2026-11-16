// Appended to harness CODE: action generation + eval term breakdown (player-zero relative).
extern "C" int gen_actions(const int *p,int *out){State s=unpack(p);prepare(&s);Action a[ACTIONS];int n=generate(&s,a);
for(int i=0;i<n;i++){out[3*i]=a[i].kind;out[3*i+1]=a[i].unit;out[3*i+2]=a[i].target;}return n;}
extern "C" int is_threat(const int *p){State s=unpack(p);prepare(&s);return material_threat(&s);}
extern "C" void eval_terms(const int *p,double *t){State st=unpack(p);prepare(&st);const State*s=&st;
for(int k=0;k<8;k++)t[k]=0;
for(int side=0;side<2;side++){double sg=side?-1:1;unsigned own=s->owned[side];
 while(own){int i=__builtin_ctz(own);own&=own-1;const Unit*u=&s->u[i];int nearest=CELLS;
  t[0]+=sg;t[1]+=sg*u->hp;unsigned targets=s->owned[1-side];if(u->leader)targets=(targets|s->owned[2])&~s->leaders;
  while(targets){int j=__builtin_ctz(targets);targets&=targets-1;int q=s->u[j].pos,d=distance_table[u->pos][q];if(d<nearest)nearest=d;if(u->leader)t[2]+=sg*proximity[u->pos][q];}
  if(nearest<CELLS){if(u->leader)t[3]+=sg*leader_bonus[nearest];else t[4]+=sg*fighter_bonus[nearest];}}}
t[5]=safety_value(s);
if(s->owned[2]){int leader[2]={-1,-1};unsigned ls=s->leaders;while(ls){int i=__builtin_ctz(ls);ls&=ls-1;if(s->u[i].owner<2)leader[s->u[i].owner]=i;}
 unsigned ns=s->owned[2];while(ns){int i=__builtin_ctz(ns);ns&=ns-1;int d[2]={recruit_distance(s,leader[0],i),recruit_distance(s,leader[1],i)};int access[2]={0,0};
  for(int pp=0;pp<2;pp++){if(d[pp]>=CELLS)continue;int risk=0;unsigned en=s->owned[1-pp]&~s->leaders;
   while(en){int j=__builtin_ctz(en);en&=en-1;int dm=7-taxi[s->u[j].pos][s->u[i].pos];if(dm>risk&&fast_hit(s,s->u[j].pos,s->u[i].pos)==i)risk=dm;}
   if(risk>s->u[i].hp)risk=s->u[i].hp;access[pp]=RECRUIT_WEIGHT*(2*s->u[i].hp-risk)/(2*s->u[i].hp*(d[pp]+2));if(RECAPTURE_DISCOUNT&&d[1-pp]==0)access[pp]/=2;}
  t[6]+=access[0]-access[1];if(RACE_WEIGHT&&d[0]!=d[1]){int w=d[0]<d[1]?0:1;if(d[w]<CELLS)t[7]+=(w?-1:1)*RACE_WEIGHT/(d[w]+2);}}}
}
