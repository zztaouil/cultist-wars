from pathlib import Path
import re
D=Path(__file__).resolve().parent
s=(D/'baseline.c').read_text()
s=s.replace('#ifndef RECRUIT_WEIGHT', '''#ifndef LEADER_VALUE
#define LEADER_VALUE 0
#endif
#ifndef SAFETY_WEIGHT
#define SAFETY_WEIGHT 1
#endif
#ifndef RACE_WEIGHT
#define RACE_WEIGHT 60
#endif
#ifndef DEFENSIVE_PRUNING
#define DEFENSIVE_PRUNING 1
#endif
#ifndef CYCLE_Q
#define CYCLE_Q 1
#endif
static void incoming_damage(const State *s,int damage[UNITS]) {
    memset(damage,0,sizeof(int)*UNITS);
    unsigned shooters=(s->owned[0]|s->owned[1])&~s->leaders;
    unsigned live=s->owned[0]|s->owned[1]|s->owned[2];
    while(shooters) {
        int i=__builtin_ctz(shooters);shooters&=shooters-1;
        unsigned targets=live&~(1u<<i);
        while(targets) {
            int j=__builtin_ctz(targets);targets&=targets-1;
            if(taxi[s->u[i].pos][s->u[j].pos]>6)continue;
            int k=fast_hit(s,s->u[i].pos,s->u[j].pos);
            if(k<0 || s->u[k].owner==s->u[i].owner)continue;
            int d=7-taxi[s->u[i].pos][s->u[k].pos];
            if(d>damage[k])damage[k]=d;
        }
    }
}
static int safety_value(const State *s) {
    int damage[UNITS],value=0;incoming_damage(s,damage);
    for(int i=0;i<UNITS;i++)if(s->u[i].hp && s->u[i].owner<2 && damage[i]) {
        int d=damage[i],hp=s->u[i].hp;
        int penalty=12*d+(2*d>=hp?90:0);
        value+=(s->u[i].owner?1:-1)*penalty;
    }
    return value;
}
static int exposed_side(const State *s) {
    int d[UNITS];incoming_damage(s,d);
    unsigned own=s->owned[s->side];
    while(own){int i=__builtin_ctz(own);own&=own-1;if(d[i])return 1;}
    return 0;
}
static int conversion_contest(const State *s) {
    int a=-1,b=-1;
    for(int i=0;i<UNITS;i++)if(s->u[i].hp && s->u[i].leader){if(s->u[i].owner==0)a=i;else if(s->u[i].owner==1)b=i;}
    if(a<0 || b<0)return 0;
    for(int i=0;i<UNITS;i++)if(s->u[i].hp && !s->u[i].leader && s->u[i].owner<2
      && taxi[s->u[a].pos][s->u[i].pos]==1 && taxi[s->u[b].pos][s->u[i].pos]==1)return 1;
    return 0;
}
#ifndef RECRUIT_WEIGHT''')
s=s.replace('value+=access[0]-access[1];','''value+=access[0]-access[1];
        if(RACE_WEIGHT && d[0]!=d[1]) {
            int winner=d[0]<d[1]?0:1;
            if(d[winner]<CELLS)value+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);
        }''')
s=s.replace('(u->leader ? 420 : 0)','(u->leader ? LEADER_VALUE : 0)')
s=s.replace('int value = score[0] - score[1];','int value = score[0] - score[1];\n    if(SAFETY_WEIGHT)value+=SAFETY_WEIGHT*safety_value(s);')
s=s.replace('if (!depth) return quiescence(s, alpha, beta, 4);','if (!depth) return quiescence(s, alpha, beta, 4 + (CYCLE_Q && conversion_contest(s) ? 2 : 0));')
s=s.replace('&& s->remaining > 4 && abs(alpha) < WIN / 2;','&& s->remaining > 6 && abs(alpha) < WIN / 2\n        && (!DEFENSIVE_PRUNING || (!material_threat(s) && !exposed_side(s)));')
# Preserve string literals, strip comments to recover submission space.
s=re.sub(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*',lambda m:m[0] if m[0].startswith('"') else '',s)
s='\n'.join(line.strip() for line in s.splitlines() if line.strip())+'\n'
(D/'candidate.c').write_text(s)
print('candidate bytes',len(s.encode()))
