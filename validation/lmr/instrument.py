"""Build an instrumented baseline without changing production bot.c."""
from pathlib import Path
D=Path(__file__).resolve().parent
s=(D/'baseline.c').read_text()
s=s.replace('static uint64_t qnodes, pruned;', '''static uint64_t qnodes, pruned;
#ifndef PROFILE
#define PROFILE 1
#endif
#ifndef LMR
#define LMR 0
#endif
static int use_lmr = LMR;
static int profile_clock = 0;
typedef struct {
    uint64_t main[33], generated[33], expanded[33], searched[33], beta[33], first[33];
    uint64_t qleft[5], unstable, unstable_moves, qtt_cut, tt_cut;
    uint64_t pvs, pvs_nodes, aspiration, aspiration_nodes;
    uint64_t q_ns, threat_calls, threat_ns, screen_calls, screen_ns;
    uint64_t reductions, researches, research_nodes;
} Metrics;
static Metrics metrics;
#if PROFILE
#define COUNT(x) (++metrics.x)
#define ADD(x,n) (metrics.x += (n))
#define STAMP() (profile_clock ? now_ns() : 0)
#else
#define COUNT(x) ((void)0)
#define ADD(x,n) ((void)0)
#define STAMP() 0ULL
#endif''')
s=s.replace('++qnodes;', '++qnodes; COUNT(qleft[left]);')
s=s.replace('++hits; return entry->value;', '++hits; COUNT(qtt_cut); return entry->value;')
s=s.replace('int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));', '''uint64_t threat_started = STAMP();
    int unstable = 0;
    if (use_threat_q && left) {
        if (s->remaining <= left) unstable = 1;
        else { COUNT(threat_calls); unstable = material_threat(s); }
    }
    ADD(threat_ns, STAMP() - threat_started);
    if (unstable) COUNT(unstable);''')
s=s.replace('State next = apply(s, a);\n        value = -quiescence', 'if (unstable) COUNT(unstable_moves);\n        State next = apply(s, a);\n        value = -quiescence')
s=s.replace('if (!depth) return quiescence(s, alpha, beta, 4);', '''if (!depth) {
        uint64_t started = STAMP();
        int result = quiescence(s, alpha, beta, 4);
        ADD(q_ns, STAMP() - started);
        return result;
    }
    COUNT(main[depth]);''')
s=s.replace('{ *chosen = hint; return entry->value; }', '{ COUNT(tt_cut); *chosen = hint; return entry->value; }')
s=s.replace('Action actions[ACTIONS]; int n = generate(s, actions);\n    for', 'Action actions[ACTIONS]; int n = generate(s, actions);\n    COUNT(expanded[depth]); ADD(generated[depth], n);\n    for')
s=s.replace('State next = apply(s, actions[i]); Action ignored;', 'COUNT(searched[depth]);\n        State next = apply(s, actions[i]); Action ignored;')
s=s.replace('if (!stopped && value > alpha && value < beta)\n                value = -search(&next, depth - 1, -beta, -alpha, &ignored);', '''if (!stopped && value > alpha && value < beta) {
                COUNT(pvs); uint64_t before = nodes;
                value = -search(&next, depth - 1, -beta, -alpha, &ignored);
                ADD(pvs_nodes, nodes - before);
            }''')
s=s.replace('++cutoffs;\n            if (use_history', '++cutoffs; COUNT(beta[depth]); if (i == 0) COUNT(first[depth]);\n            if (use_history')
s=s.replace('nodes = cutoffs = hits = qnodes = pruned = 0; stopped = 0;', 'nodes = cutoffs = hits = qnodes = pruned = 0; stopped = 0;\n    memset(&metrics, 0, sizeof metrics);')
s=s.replace('value = search(s, depth, alpha, beta, &candidate);', 'uint64_t before = nodes;\n            value = search(s, depth, alpha, beta, &candidate);')
s=s.replace('window = window >= 1600', 'COUNT(aspiration); ADD(aspiration_nodes, nodes - before);\n            window = window >= 1600')
(D/'instrumented.c').write_text(s)
