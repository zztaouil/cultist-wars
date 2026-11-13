#define BOT_TEST
#include "../bot.c"
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
    if (!depth) return evaluate(s);
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
    /* Every endpoint pair, with every possible single blocker, both directions. */
    int occ[CELLS];
    for (int a = 0; a < CELLS; ++a) for (int b = 0; b < CELLS; ++b) if (a != b) {
        for (int p = 0; p < CELLS; ++p) occ[p] = -1;
        occ[a] = 0; occ[b] = 1;
        for (int p = 0; p < CELLS; ++p) if (p != a && p != b) {
            occ[p] = 2;
            assert(bullet_hit(a, b, occ) == reference_hit(a, b, occ));
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
    puts("PASS: exhaustive ray blockers, transitions, endgame, alpha-beta vs minimax, winning conversion");
    return 0;
}
