#define BOT_TEST
#include "candidate.c"
#include <assert.h>

static State fixture(void) {
    State s = {0}; s.remaining = 100;
    s.u[0] = (Unit){39, 10, 0, 1};
    s.u[1] = (Unit){51, 10, 1, 1};
    return s;
}
static int reference_hit(int a, int b, const int occ[CELLS]) {
    int reverse = a / W >= b / W;
    int start = reverse ? b : a, end = reverse ? a : b;
    int x = start % W, y = start / W, dx = abs(end % W - x), dy = abs(end / W - y);
    int sx = x < end % W ? 1 : -1, sy = y < end / W ? 1 : -1, err = dx - dy;
    int result = occ[b];
    for (;;) {
        int e = 2 * err;
        if (e > -dy) { err -= dy; x += sx; }
        if (e < dx) { err += dx; y += sy; }
        int p = y * W + x;
        if (p == end) break;
        if (wall[p] || occ[p] >= 0) {
            result = wall[p] ? -1 : occ[p];
            if (!reverse) break;
        }
    }
    return result;
}
static int brute(const State *s, int depth) {
    int v;
    if (terminal(s, &v)) return v;
    if (!depth) return quiescence(s, -INF, INF, 4);
    Action actions[ACTIONS]; int n = generate(s, actions), best = -INF;
    for (int i = 0; i < n; ++i) {
        State next = apply(s, actions[i]);
        v = -brute(&next, depth - 1);
        if (v > best) best = v;
    }
    return best;
}
int main(void) {
    initialize();
    use_futility = 0;
    /* Every endpoint pair, with every possible single blocker, both directions. */
    int occ[CELLS];
    for (int a = 0; a < CELLS; ++a) for (int b = 0; b < CELLS; ++b) if (a != b) {
        for (int p = 0; p < CELLS; ++p) occ[p] = -1;
        occ[a] = 0; occ[b] = 1;
        for (int p = 0; p < CELLS; ++p) if (p != a && p != b) {
            occ[p] = 2;
            assert(bullet_hit(a, b, occ) == reference_hit(a, b, occ));
            State ray_state = {0};
            ray_state.u[0] = (Unit){a,10,0,0};
            ray_state.u[1] = (Unit){b,10,1,0};
            ray_state.u[2] = (Unit){p,10,2,0};
            prepare(&ray_state);
            assert(fast_hit(&ray_state,a,b) == reference_hit(a,b,occ));
            occ[p] = -1; wall[p] = 1;
            assert(bullet_hit(a, b, occ) == reference_hit(a, b, occ));
            wall[p] = 0;
        }
    }
    State s = fixture(); s.u[2] = (Unit){40, 10, 2, 0};
    State next = apply(&s, (Action){CONVERT, 0, 2, 0});
    assert(next.u[2].owner == 0 && next.side == 1 && next.remaining == 99);
    assert(s.u[2].owner == 2);
    s.u[2] = (Unit){40, 10, 0, 0}; s.u[3] = (Unit){42, 5, 1, 0};
    next = apply(&s, (Action){SHOOT, 2, 3, 0});
    assert(next.u[3].hp == 0);
    occupancy(&next, occ); assert(occ[42] == -1);
    s.u[4] = (Unit){41, 10, 0, 0};
    next = apply(&s, (Action){SHOOT, 2, 3, 0});
    assert(next.u[4].hp == 4 && next.u[3].hp == 5);
    s = fixture(); s.u[0].hp = 0; s.u[2] = (Unit){40, 10, 0, 0};
    int value; assert(!terminal(&s, &value));
    s.remaining = 0; assert(terminal(&s, &value) && value == 0);
    s.u[3] = (Unit){42, 1, 0, 0}; assert(terminal(&s, &value) && value == WIN);
    for (int k = 0; k < 12; ++k) {
        s = fixture(); s.side = k % 2;
        s.u[2] = (Unit){40 + k % 3, 3 + k % 7, 0, 0};
        s.u[3] = (Unit){44 + k % 3, 2 + k % 8, 1, 0};
        s.u[4] = (Unit){27, 10, 2, 0};
        memset(tt, 0, sizeof tt); stopped = 0; nodes = 0; deadline = now_ns() + 10000000000ULL;
        Action a; int exact = brute(&s, 3);
        assert(search(&s, 3, -INF, INF, &a) == exact && !stopped);
        State child = apply(&s, a); assert(-brute(&child, 2) == exact);
        assert(search(&s, 3, -INF, INF, &a) == exact);
    }
    s = fixture(); s.remaining = 1; s.u[2] = (Unit){40, 10, 2, 0};
    int depth, advantage; Action a = choose(&s, &depth, &advantage);
    assert(a.kind == CONVERT && a.target == 2 && advantage == WIN);
    /* Java Random sequence, dead-neutral selection and trapped WAIT draw. */
    s = fixture(); s.rng = 42; s.rng_known = 1;
    assert(random_next(&s, 12) == 3 && s.rng == 1250496027u);
    assert(random_next(&s, 4) == 0 && s.rng == 1116302264u);
    s = fixture(); s.rng = 42; s.rng_known = 1;
    s.neutral_mask = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
    s.u[5] = (Unit){20, 10, 2, 0};
    next = apply(&s, (Action){WAIT, 0, 0, 0});
    assert(next.u[5].pos == 7 && next.rng == 1116302264u);
    for (int d = 0; d < 4; ++d) wall[neighbor(20, d)] = 1;
    next = apply(&s, (Action){WAIT, 0, 0, 0});
    assert(next.u[5].pos == 20 && next.rng == 1116302264u);
    for (int d = 0; d < 4; ++d) wall[neighbor(20, d)] = 0;
    s.u[5].hp = 0;
    next = apply(&s, (Action){WAIT, 0, 0, 0});
    assert(next.rng == 1250496027u); /* dead selected: no direction draw */
    s.neutral_mask = 1u << 2;
    next = apply(&s, (Action){WAIT, 0, 0, 0});
    assert(next.rng == 1250496027u); /* index outside shortened list */
    s.neutral_mask = 0;
    next = apply(&s, (Action){WAIT, 0, 0, 0}); assert(next.rng == 42);
    /* Corpse blocks neutral's north cell until cleanup. */
    s.neutral_mask = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
    s.u[5] = (Unit){20, 10, 2, 0};
    s.u[6] = (Unit){7, 0, 1, 0};
    move_neutral(&s, 6);
    assert(s.u[5].pos == 19); /* draw % 3 == 2: west */
    State other = s; other.rng++;
    assert(hash_state(&s) != hash_state(&other));
    other = s; other.neutral_mask ^= 1u << 2;
    assert(hash_state(&s) != hash_state(&other));
    /* Follow both players across real neutral moves and opponent conversions. */
    s = fixture(); s.remaining = 150; s.rng = 42; s.rng_known = 1;
    s.neutral_mask = ((1u << UNITS) - 1) & ~3u;
    for (int i = 2; i < UNITS; ++i) s.u[i] = (Unit){14 + i, 10, 2, 0};
    Tracker trackers[2] = {0};
    for (int turn = 0; turn < 40; ++turn) {
        int side = s.side; State observed = s;
        observed.rng_known = 0; observed.rng = 0; observed.neutral_mask = 0;
        track_observation(&trackers[side], &observed, turn < 2);
        assert(trackers[side].n > 0);
        int found = 0;
        for (int k = 0; k < trackers[side].n; ++k)
            if (trackers[side].states[k].rng == s.rng) found = 1;
        assert(found);
        Action actions[ACTIONS]; int n = generate(&s, actions);
        Action selected = actions[(turn * 17 + 3) % n];
        track_action(&trackers[side], selected);
        s = apply(&s, selected);
    }
    /* Tactical leaf sees a lethal shot beyond nominal search horizon. */
    s = fixture(); s.u[1] = (Unit){42, 1, 1, 0};
    s.u[2] = (Unit){40, 10, 0, 0};
    stopped = 0; deadline = now_ns() + 10000000000ULL;
    assert(quiescence(&s, -INF, INF, 4) == WIN);
    /* Stand-pat must not hide a forced enemy conversion. */
    memset(wall, 0, sizeof wall); wall[13] = 1; initialize();
    s = (State){0}; s.remaining = 20;
    s.u[0] = (Unit){0,1,0,0}; s.u[1] = (Unit){1,10,1,1}; prepare(&s);
    stopped = 0; deadline = now_ns() + 10000000000ULL;
    assert(material_threat(&s));
    assert(quiescence(&s, -INF, INF, 4) == -WIN);
    /* A quiet retreat escapes that conversion; tactical-only leaves miss it. */
    wall[13] = 0; initialize(); prepare(&s);
    assert(quiescence(&s, -INF, INF, 4) > -WIN);
    /* A different aim point can kill a victim hidden on its direct ray. */
    wall[1] = 1; initialize(); s = (State){0}; s.remaining = 20;
    s.u[0] = (Unit){0,10,1,0}; s.u[1] = (Unit){15,4,0,0};
    s.u[2] = (Unit){29,10,2,0}; prepare(&s);
    assert(fast_hit(&s,0,15) == -1 && fast_hit(&s,0,29) == 1);
    assert(material_threat(&s));
    memset(wall, 0, sizeof wall); initialize();
    /* At the actual limit, equal counts draw despite unequal HP/leader bonus. */
    s = fixture(); s.remaining = 1; s.u[1].hp = 1; prepare(&s);
    memset(qtt, 0, sizeof qtt);
    assert(evaluate(&s) > 0);
    assert(quiescence(&s, -INF, INF, 4) == 0);
    /* Fast threat detector agrees with complete opponent tactical generation. */
    srand(1928);
    for (int trial = 0; trial < 2000; ++trial) {
        s = (State){0}; s.remaining = 50; s.side = trial % 2;
        unsigned char used[CELLS] = {0};
        for (int i = 0; i < UNITS; ++i) {
            int pos; do { pos = rand() % CELLS; } while (used[pos]);
            used[pos] = 1;
            s.u[i] = (Unit){pos,1 + rand() % 10,i < 2 ? i : rand() % 3,i < 2};
        }
        prepare(&s); State opponent = s; opponent.side ^= 1;
        Action attacks[ACTIONS]; int n = generate_mode(&opponent,attacks,1), expected = 0;
        for (int i = 0; i < n; ++i) {
            Action a = attacks[i];
            if (a.kind == CONVERT && s.u[a.target].owner == s.side) expected = 1;
            if (a.kind == SHOOT) {
                int hit = fast_hit(&s,s.u[a.unit].pos,s.u[a.target].pos);
                if (hit >= 0 && s.u[hit].owner == s.side
                        && s.u[hit].hp <= 7 - taxi[s.u[a.unit].pos][s.u[hit].pos]) expected = 1;
            }
        }
        assert(material_threat(&s) == expected);
    }
    /* History stays bounded and does not leak across side/role or maps. */
    s = fixture(); Action quiet = {MOVE,0,40,0};
    for (int i = 0; i < 1000; ++i) update_history(&s,quiet,256);
    assert(history[0][1][39][40] == 2048 && history[0][0][39][40] == 0);
    assert(history[1][1][39][40] == 0);
    for (int i = 0; i < 1000; ++i) update_history(&s,quiet,-256);
    assert(history[0][1][39][40] == -2048);
    initialize(); assert(history[0][1][39][40] == 0);
    use_futility = 1;
    puts("PASS: rays, transitions, endgame, minimax, quiescence, neutral RNG, observation tracking");
    return 0;
}
