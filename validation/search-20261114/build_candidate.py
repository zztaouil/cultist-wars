from pathlib import Path
D=Path(__file__).resolve().parent
s=(D/'live.c').read_text()
s=s.replace('uint8_t depth, flag; } QEntry;', 'uint8_t depth, flag; uint16_t move; } QEntry;')
a=s.index('static int quiescence(');b=s.index('\n#define NF',a)
s=s[:a]+'''/* The cached move uses existing QEntry padding (16-byte entries). Bounds
   remain valid only at exactly the same tactical horizon. */
static uint16_t qmove(Action a) {
    return (uint16_t)(1 + a.kind + 4 * a.unit + 64 * a.target);
}
static int quiescence(const State *s, int alpha, int beta, int left) {
    State copy;
    if (!s->ready) { copy = *s; prepare(&copy); s = &copy; }
    ++qnodes;
    if (check_time()) return 0;
    int value;
    if (terminal(s, &value)) return value;
    int original_alpha = alpha, original_beta = beta;
    QEntry *entry = &qtt[s->key & (TT_SIZE - 1)];
    uint16_t hint = entry->key == s->key ? entry->move : 0;
    if (entry->key == s->key && entry->depth == left + 1) {
        ++hits;
        if (entry->flag == 0) return entry->value;
        if (entry->flag == 1 && entry->value > alpha) alpha = entry->value;
        if (entry->flag == 2 && entry->value < beta) beta = entry->value;
        if (alpha >= beta) return entry->value;
    }
    int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));
    int best = unstable ? -INF : evaluate_live(s);
    if (!left || (!unstable && best >= beta)) {
        *entry = (QEntry){s->key,best,left+1,!left ? 0 : 1,0};
        return best;
    }
    if (best > alpha) alpha = best;
    Action actions[ACTIONS]; int n = generate_mode(s, actions, !unstable);
    uint16_t best_move = 0;
    for (int i = 0; i < n; ++i)
        if (qmove(actions[i]) == hint) actions[i].order += 1000000;
    for (int i = 0; i < n; ++i) {
        int k = i;
        for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
        Action tmp = actions[i]; actions[i] = actions[k]; actions[k] = tmp;
        State next = apply(s, actions[i]);
        value = -quiescence(&next, -beta, -alpha, left - 1);
        if (stopped) return 0;
        if (value > best) { best = value; best_move = qmove(actions[i]); }
        if (value > alpha) alpha = value;
        if (alpha >= beta) { ++cutoffs; break; }
    }
    *entry = (QEntry){s->key,best,left+1,best <= original_alpha ? 2 : best >= original_beta ? 1 : 0,best_move};
    return best;
}
''' + s[b:]
# Keep standalone source below the judge's 100 KB cap.
import re
s=re.sub(r'/\*.*?\*/', '', s, flags=re.S)
(D/'candidate.c').write_text(s)
print(len(s))
