from pathlib import Path
D=Path(__file__).resolve().parent
s=(D/'baseline.c').read_text()
hp='''#ifndef EVAL_HP
#define EVAL_HP 1
#endif
/* Full health keeps baseline value. Jumps at 4/7 HP reflect surviving
   common 3/6-damage hits; role-specific fragility is an experimental prior. */
static const int cultist_hp[11] = {0,-190,-160,-125,-80,-40,0,70,100,130,180};
static const int leader_hp[11] = {0,-240,-200,-160,-100,-60,-20,60,100,140,180};
'''
e=s.replace('static int evaluate_live(',hp+'static int evaluate_live(',1).replace('18 * u->hp','(EVAL_HP ? (u->leader ? leader_hp[u->hp] : cultist_hp[u->hp]) : 18 * u->hp)')
(D/'eval-hp.c').write_text(e)
q=s.replace('int best = evaluate_live(s);\n    int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));','''int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));
    /* Threatened nodes cannot stand pat: their static value is discarded. */
    int best = unstable ? -INF : evaluate_live(s);''')
q=q.replace('static int quiescence(', '#ifndef Q_LAZY_EVAL\n#define Q_LAZY_EVAL 1\n#endif\nstatic int quiescence(',1).replace('int best = unstable ? -INF : evaluate_live(s);','int best = (Q_LAZY_EVAL && unstable) ? -INF : evaluate_live(s);')
(D/'q-lazy-eval.c').write_text(q)
r=s.replace('static int evaluate_live(','''#ifndef EVAL_RACE
#define EVAL_RACE 1
#endif
/* Arrival includes the conversion action; precomputed BFS ignores occupancy.
   One leader acts per side, so distant opportunities are discounted. */
static int recruitment_value(const State *s) {
    int leader[2] = {-1,-1};
    for (int side=0;side<2;side++) {
        unsigned mask=s->owned[side]&s->leaders;
        if(mask) leader[side]=__builtin_ctz(mask);
    }
    int value=0; unsigned neutral=s->owned[2];
    while(neutral) {
        int j=__builtin_ctz(neutral);neutral&=neutral-1;
        int arrival[2]={2*CELLS,2*CELLS};
        for(int side=0;side<2;side++) if(leader[side]>=0) {
            int d=distance_table[s->u[leader[side]].pos][s->u[j].pos];
            if(d<CELLS) arrival[side]=2*d-(s->side==side);
        }
        value+=180*(arrival[1]-arrival[0])/(arrival[0]+arrival[1]+4);
    }
    return value;
}
static int evaluate_live(''',1)
r=r.replace('uint64_t key = s->key ^ s->metadata;', 'uint64_t key = s->key ^ s->metadata;\n    if (EVAL_RACE) key ^= mix64(0xabc123ULL + s->side);')
r=r.replace('int value = score[0] - score[1];','int value = score[0] - score[1];\n    if (EVAL_RACE) value += recruitment_value(s);')
(D/'eval-race.c').write_text(r)
