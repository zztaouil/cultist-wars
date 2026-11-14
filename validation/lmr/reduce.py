"""Apply the isolated, switchable one-ply LMR experiment."""
from pathlib import Path
D=Path(__file__).resolve().parent
s=(D/'instrumented.c').read_text()
pos=s.index('/* Bounded tactical extension.')
s=s[:pos]+'''/* Broad screening also protects immediate neutral recruitment. No action is
   removed: this predicate only decides whether a quiet move may be reduced. */
static int lmr_creates_threat(const State *next) {
    if (material_threat(next)) return 1;
    unsigned leaders = next->owned[1-next->side] & next->leaders;
    while (leaders) {
        int i = __builtin_ctz(leaders); leaders &= leaders - 1;
        unsigned neutrals = next->owned[2] & ~next->leaders;
        while (neutrals) {
            int j = __builtin_ctz(neutrals); neutrals &= neutrals - 1;
            if (taxi[next->u[i].pos][next->u[j].pos] == 1) return 1;
        }
    }
    return 0;
}
''' +s[pos:]
s=s.replace('Action quiets[UNITS * 4]; int quiet_count = 0;', '''Action quiets[UNITS * 4]; int quiet_count = 0;
    /* Original window identifies non-PV calls even after TT narrowing. Exclude
       any nominal+quiescence path reaching the actual final-action horizon. */
    int lmr_node = use_lmr && depth >= 3 && original_beta == original_alpha + 1
        && s->remaining > depth + 4 && abs(alpha) < WIN / 2;
    int threatened = -1;''')
s=s.replace('value = -search(&next, depth - 1, -alpha - 1, -alpha, &ignored);', '''int reduce = 0;
            if (lmr_node && i >= 3 && actions[i].kind == MOVE
                    && !s->u[actions[i].unit].leader && !same(actions[i], hint)) {
                uint64_t started = STAMP();
                if (threatened < 0) { COUNT(screen_calls); threatened = material_threat(s); }
                if (!threatened) { COUNT(screen_calls); reduce = !lmr_creates_threat(&next); }
                ADD(screen_ns, STAMP() - started);
            }
            if (reduce) {
                COUNT(reductions);
                value = -search(&next, depth - 2, -alpha - 1, -alpha, &ignored);
                /* A fail-low can still be a false negative. This is selective
                   search, not a minimax-preserving transformation. */
                if (!stopped && value > alpha) {
                    COUNT(researches); uint64_t before = nodes;
                    value = -search(&next, depth - 1, -alpha - 1, -alpha, &ignored);
                    ADD(research_nodes, nodes - before);
                }
            } else value = -search(&next, depth - 1, -alpha - 1, -alpha, &ignored);''')
(D/'experiment.c').write_text(s)
