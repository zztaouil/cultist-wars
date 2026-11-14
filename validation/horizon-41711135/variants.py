"""Isolated variants derived from immutable submitted source."""
import hashlib,json
from pathlib import Path
D=Path(__file__).resolve().parent
base=(D/'baseline.c').read_text()
contest='''
/* A fighter adjacent to both leaders can change ownership on every action.
   At the static horizon, average its material ownership rather than reward
   whichever leader happened to move last. Board-only: eval-cache key unchanged. */
#ifndef CONTEST_EVAL
#define CONTEST_EVAL 1
#endif
static int contested_unit(const State *s, int i) {
    if (s->u[i].leader || s->u[i].owner > 1 || !s->u[i].hp) return 0;
    unsigned leaders = s->leaders & (s->owned[0] | s->owned[1]);
    unsigned adjacent_sides = 0;
    while (leaders) {
        int j = __builtin_ctz(leaders); leaders &= leaders - 1;
        if (taxi[s->u[i].pos][s->u[j].pos] == 1)
            adjacent_sides |= 1u << s->u[j].owner;
    }
    return adjacent_sides == 3;
}
'''
def eval_variant(s):
 s=s.replace('static int evaluate_live(const State *s) {',contest+'\nstatic int evaluate_live(const State *s) {')
 needle='score[side] += 1000 + 18 * u->hp + (u->leader ? 420 : 0);'
 assert needle in s
 return s.replace(needle,needle+'\n            if (CONTEST_EVAL && contested_unit(s, i)) score[side] -= 1000 + 18 * u->hp;')
def extend_variant(s):
 begin='static int quiescence(const State *s, int alpha, int beta, int left) {'
 assert s.count(begin)==1
 s=s.replace(begin,'''#ifndef THREAT_EXTENSION
#define THREAT_EXTENSION 1
#endif
static int quiescence_extra(const State *s, int alpha, int beta, int left, int extra) {''')
 begin=s.index('static int quiescence_extra(');end=s.index('#ifndef NN_ENABLE',begin)
 q=s[begin:end]
 q=q.replace('int original_alpha = alpha, original_beta = beta;', '''/* At the normal Q boundary, allow one two-ply response to a material
       threat. Extra allowance is part of the Q-cache depth identity. */
    if (!left && extra && material_threat(s)) { left = 2; extra = 0; }
    int qdepth = left + 1 + extra * 8;
    int original_alpha = alpha, original_beta = beta;''')
 q=q.replace('entry->depth == left + 1','entry->depth == qdepth').replace('best,left+1','best,qdepth').replace('best, left+1','best, qdepth').replace('best,left + 1','best,qdepth').replace('best, left + 1','best, qdepth')
 q=q.replace('quiescence(&next, -beta, -alpha, left - 1)','quiescence_extra(&next, -beta, -alpha, left - 1, extra)')
 q+='\nstatic int quiescence(const State *s, int alpha, int beta, int left) {\n    return quiescence_extra(s, alpha, beta, left, THREAT_EXTENSION);\n}\n'
 return s[:begin]+q+s[end:]
variants={'contest':eval_variant(base),'extension':extend_variant(base),'combined':extend_variant(eval_variant(base))}
for name,s in variants.items():
 (D/(name+'.c')).write_text(s)
 assert len(s.encode())<100000
(D/'sources.json').write_text(json.dumps({k:dict(bytes=len(s.encode()),sha256=hashlib.sha256(s.encode()).hexdigest()) for k,s in dict(baseline=base,**variants).items()},indent=2))
