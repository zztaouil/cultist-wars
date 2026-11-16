"""Make Texel candidate: regime-weighted eval terms. usage: mk_texel.py weights.json alpha out.cpp
alpha in [0,1]: geometric interpolation from production weights (alpha=0) to fitted (alpha=1)."""
import json,sys,math
from pathlib import Path
src=(Path(__file__).resolve().parents[2]/'submissions/41715067.cpp').read_text()
import os;NEG=os.environ.get('NEG')=='1';W=json.load(open(sys.argv[1]));al=float(sys.argv[2]);W0=[1000,18,1,1,1,1,1,1]
rows=[]
for r in ('0','1','2'):
  w=W[r]['w'];row=[]
  for k in range(8):
    a,b=W0[k],w[k]
    v=(a*(b/a)**al if b>0 else a*(1-al)) if not NEG else (a+(b-a)*al)  # NEG: linear interpolation, keeps sign
    row.append(int(round(v*1000)))
  rows.append(row)
tab='static const long long TEXEL_W[3][8]={'+','.join('{'+','.join(map(str,r))+'}' for r in rows)+'};\n'
old_rc='static int recruit_control(const State *s) {'
assert old_rc in src
src=src.replace(old_rc,'static int recruit_control2(const State *s,int *race_out);\nstatic int recruit_control(const State *s){int r;int a=recruit_control2(s,&r);return a+r;}\nstatic int recruit_control2(const State *s,int *race_out) {',1)
# split race accumulation
old='value+=access[0]-access[1];\nif(RACE_WEIGHT && d[0]!=d[1]) {\nint winner=d[0]<d[1]?0:1;\nif(d[winner]<CELLS)value+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);\n}\n}\nreturn value;'
assert old in src
src=src.replace(old,'value+=access[0]-access[1];\nif(RACE_WEIGHT && d[0]!=d[1]) {\nint winner=d[0]<d[1]?0:1;\nif(d[winner]<CELLS)race+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);\n}\n}\n*race_out=race;return value;')
src=src.replace('static int recruit_control2(const State *s,int *race_out) {\nint leader[2]={-1,-1},value=0;','static int recruit_control2(const State *s,int *race_out) {\nint leader[2]={-1,-1},value=0,race=0;')
assert 'value=0,race=0' in src
# evaluate_live body
a=src.index('int score[2] = {0,0};');b=src.index('*entry = EvalEntry{key,value,1};')
body='''long long t[8]={0,0,0,0,0,0,0,0};
for (int side = 0; side < 2; ++side) {
int sg=side?-1:1;unsigned own = s->owned[side];
while (own) {
int i = __builtin_ctz(own); own &= own - 1;
const Unit *u = &s->u[i]; int nearest = CELLS;
t[0]+=sg;t[1]+=sg*u->hp;
unsigned targets = s->owned[1-side];
if (u->leader) targets = (targets | s->owned[2]) & ~s->leaders;
while (targets) {
int j = __builtin_ctz(targets); targets &= targets - 1;
int p = s->u[j].pos, dist = distance_table[u->pos][p];
if (dist < nearest) nearest = dist;
if (u->leader) t[2]+=sg*proximity[u->pos][p];
}
if (nearest < CELLS) { if(u->leader) t[3]+=sg*leader_bonus[nearest]; else t[4]+=sg*fighter_bonus[nearest]; }
}
}
if(SAFETY_WEIGHT)t[5]=SAFETY_WEIGHT*safety_value(s);
if(RECRUIT_WEIGHT && s->owned[2]){int r;t[6]=recruit_control2(s,&r);t[7]=r;}
int nn_=__builtin_popcount((unsigned)s->owned[2]),nl_=0;{unsigned ls=s->leaders;while(ls){int i=__builtin_ctz(ls);ls&=ls-1;if(s->u[i].owner<2)nl_++;}}
int rg=nn_==0?2:(nl_==2?0:1);long long acc=0;for(int k=0;k<8;k++)acc+=TEXEL_W[rg][k]*t[k];
int value=(int)(acc/1000);
'''
src=src[:a]+body+src[b:]
src=src.replace('static int evaluate_live(const State *s) {',tab+'static int evaluate_live(const State *s) {',1)
Path(sys.argv[3]).write_text(src);print(rows)
