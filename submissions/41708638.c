#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 13
#define H 7
#define CELLS (W * H)
#define UNITS 14
#define ACTIONS (1 + UNITS * (4 + UNITS))
#define INF 10000000
#define WIN 1000000
#define TT_SIZE 65536
#ifndef BUDGET_MS
#define BUDGET_MS 35
#endif

typedef struct { int pos, hp, owner, leader; } Unit;
typedef struct { Unit u[UNITS]; int side, remaining; } State;
enum { WAIT, MOVE, SHOOT, CONVERT };
typedef struct { int kind, unit, target, order; } Action;
typedef struct { uint64_t key; int depth, value, flag; Action best; } Entry;
typedef struct { unsigned char length, cell[CELLS]; } Ray;
static int wall[CELLS], distance_table[CELLS][CELLS];
static Ray rays[CELLS][CELLS];
static Entry tt[TT_SIZE];
static uint64_t deadline, nodes, cutoffs, hits;
static int stopped;

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static int manhattan(int a, int b) { return abs(a % W - b % W) + abs(a / W - b / W); }
static int neighbor(int p, int d) {
    static const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
    int x = p % W + dx[d], y = p / W + dy[d];
    return x < 0 || x >= W || y < 0 || y >= H ? -1 : y * W + x;
}
/* Match Board.java's strict tie tests and lower-Y traversal exactly.
   Reverse the resulting cells when the shooter is the high-Y endpoint. */
static void initialize(void) {
    for (int a = 0; a < CELLS; ++a) {
        for (int b = 0; b < CELLS; ++b) {
            Ray *r = &rays[a][b]; r->length = 0;
            if (a == b) continue;
            int forward = a / W < b / W;
            int start = forward ? a : b, end = forward ? b : a;
            int x = start % W, y = start / W, ex = end % W, ey = end / W;
            int dx = abs(ex - x), dy = abs(ey - y);
            int sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1, err = dx - dy;
            unsigned char path[CELLS]; int n = 0;
            path[n++] = (unsigned char)start;
            while (x != ex || y != ey) {
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
                path[n++] = (unsigned char)(y * W + x);
            }
            for (int i = 1; i < n; ++i)
                r->cell[r->length++] = path[forward ? i : n - 1 - i];
        }
        int queue[CELLS], head = 0, tail = 0;
        for (int b = 0; b < CELLS; ++b) distance_table[a][b] = CELLS;
        if (wall[a]) continue;
        distance_table[a][a] = 0; queue[tail++] = a;
        while (head < tail) {
            int p = queue[head++];
            for (int d = 0; d < 4; ++d) {
                int q = neighbor(p, d);
                if (q >= 0 && !wall[q] && distance_table[a][q] == CELLS) {
                    distance_table[a][q] = distance_table[a][p] + 1;
                    queue[tail++] = q;
                }
            }
        }
    }
}
static void occupancy(const State *s, int occ[CELLS]) {
    for (int p = 0; p < CELLS; ++p) occ[p] = -1;
    for (int i = 0; i < UNITS; ++i) if (s->u[i].hp > 0) occ[s->u[i].pos] = i;
}
static int bullet_hit(int from, int to, const int occ[CELLS]) {
    const Ray *r = &rays[from][to];
    for (int i = 0; i < r->length; ++i) {
        int p = r->cell[i];
        if (wall[p]) return -1;
        if (occ[p] >= 0) return occ[p];
    }
    return -1;
}
static int terminal(const State *s, int *value) {
    int count[2] = {0, 0};
    for (int i = 0; i < UNITS; ++i)
        if (s->u[i].hp > 0 && s->u[i].owner < 2) ++count[s->u[i].owner];
    if (!count[0] || !count[1] || s->remaining <= 0) {
        int diff = count[s->side] - count[1 - s->side];
        *value = diff > 0 ? WIN : diff < 0 ? -WIN : 0;
        return 1;
    }
    return 0;
}
/* Advantage combines material, recruitment distance, leader survival,
   best available shot, exposure and mobility. All scores are antisymmetric. */
static int evaluate(const State *s) {
    int value;
    if (terminal(s, &value)) return value;
    int score[2] = {0, 0}, occ[CELLS], best_shot[2] = {0, 0};
    occupancy(s, occ);
    for (int i = 0; i < UNITS; ++i) {
        const Unit *u = &s->u[i];
        if (u->hp <= 0 || u->owner == 2) continue;
        int side = u->owner, nearest = CELLS, danger = 0;
        score[side] += 1000 + 18 * u->hp + (u->leader ? 420 : 0);
        for (int d = 0; d < 4; ++d) {
            int p = neighbor(u->pos, d);
            if (p >= 0 && !wall[p] && occ[p] < 0) score[side] += 3;
        }
        for (int j = 0; j < UNITS; ++j) {
            const Unit *v = &s->u[j];
            if (v->hp <= 0 || v->owner == side) continue;
            int dist = manhattan(u->pos, v->pos);
            int path = distance_table[u->pos][v->pos];
            if ((u->leader && !v->leader) || (!u->leader && v->owner != 2)) {
                if (path < nearest) nearest = path;
                if (u->leader) score[side] += 100 / (1 + path);
            }
            if (!u->leader && v->owner != 2 && dist <= 6 && bullet_hit(u->pos, v->pos, occ) == j) {
                int shot = (7 - dist) * 22 + (v->hp <= 7 - dist ? 300 : 0);
                if (shot > best_shot[side]) best_shot[side] = shot;
            }
            if (v->owner != 2 && !v->leader && dist <= 6 && bullet_hit(v->pos, u->pos, occ) == i) {
                int threat = 7 - dist;
                if (threat > danger) danger = threat;
            }
            if (!u->leader && v->leader && v->owner != 2 && dist == 1) score[side] -= 220;
        }
        if (nearest < CELLS) score[side] += (u->leader ? 240 : 90) / (nearest + 1);
        score[side] -= danger * (u->leader ? 25 : 10);
        if (danger >= u->hp) score[side] -= u->leader ? 400 : 180;
    }
    score[0] += best_shot[0]; score[1] += best_shot[1];
    return score[s->side] - score[1 - s->side];
}
static int generate(const State *s, Action actions[ACTIONS]) {
    int occ[CELLS], n = 0; occupancy(s, occ);
    actions[n++] = (Action){WAIT, 0, 0, -100};
    for (int i = 0; i < UNITS; ++i) {
        const Unit *u = &s->u[i];
        if (u->hp <= 0 || u->owner != s->side) continue;
        for (int d = 0; d < 4; ++d) {
            int p = neighbor(u->pos, d);
            if (p >= 0 && !wall[p] && occ[p] < 0)
                actions[n++] = (Action){MOVE, i, p, 0};
        }
        unsigned seen = 0;
        for (int j = 0; j < UNITS; ++j) {
            const Unit *v = &s->u[j];
            if (v->hp <= 0 || i == j) continue;
            int dist = manhattan(u->pos, v->pos);
            if (u->leader) {
                if (!v->leader && v->owner != s->side && dist == 1)
                    actions[n++] = (Action){CONVERT, i, j, v->owner == 2 ? 2000 : 4000};
            } else if (dist <= 6) {
                int hit = bullet_hit(u->pos, v->pos, occ);
                if (hit < 0 || (seen & (1u << hit))) continue;
                seen |= 1u << hit;
                const Unit *victim = &s->u[hit];
                int damage = 7 - manhattan(u->pos, victim->pos);
                int order = damage * 40 + (damage >= victim->hp ? 1500 : 0);
                if (victim->owner == s->side) order = -order;
                if (victim->owner == 2) order /= 4;
                actions[n++] = (Action){SHOOT, i, j, order};
            }
        }
    }
    return n;
}
static State apply(const State *s, Action a) {
    State next = *s;
    if (a.kind == MOVE) next.u[a.unit].pos = a.target;
    else if (a.kind == CONVERT) next.u[a.target].owner = s->side;
    else if (a.kind == SHOOT) {
        int occ[CELLS]; occupancy(s, occ);
        int hit = bullet_hit(s->u[a.unit].pos, s->u[a.target].pos, occ);
        if (hit >= 0) {
            next.u[hit].hp -= 7 - manhattan(s->u[a.unit].pos, s->u[hit].pos);
            if (next.u[hit].hp < 0) next.u[hit].hp = 0;
        }
    }
    next.side ^= 1; --next.remaining;
    return next;
}
static uint64_t hash_state(const State *s) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < UNITS; ++i) {
        const Unit *u = &s->u[i];
        uint64_t v = u->hp > 0 ? (uint64_t)(1 + u->pos + CELLS * (u->hp + 11 * (u->owner + 3 * u->leader))) : 0;
        h = (h ^ v) * 1099511628211ULL;
    }
    h = (h ^ (unsigned)s->side) * 1099511628211ULL;
    return (h ^ (unsigned)s->remaining) * 1099511628211ULL;
}
static int same(Action a, Action b) { return a.kind == b.kind && a.unit == b.unit && a.target == b.target; }
static int search(const State *s, int depth, int alpha, int beta, Action *chosen) {
    ++nodes;
    if ((nodes & 63u) == 0 && now_ns() >= deadline) { stopped = 1; return 0; }
    int value;
    if (terminal(s, &value)) return value;
    if (!depth) return evaluate(s);
    uint64_t key = hash_state(s);
    Entry *entry = &tt[key & (TT_SIZE - 1)];
    int original_alpha = alpha, original_beta = beta;
    Action hint = { -1, 0, 0, 0 };
    if (entry->key == key && entry->depth > 0) {
        hint = entry->best;
        if (entry->depth >= depth) {
            ++hits;
            if (entry->flag == 0) { *chosen = hint; return entry->value; }
            if (entry->flag == 1 && entry->value > alpha) alpha = entry->value;
            if (entry->flag == 2 && entry->value < beta) beta = entry->value;
            if (alpha >= beta) { *chosen = hint; return entry->value; }
        }
    }
    Action actions[ACTIONS]; int n = generate(s, actions);
    for (int i = 0; i < n; ++i) if (same(actions[i], hint)) actions[i].order += 100000;
    int best = -INF; Action best_action = actions[0];
    for (int i = 0; i < n; ++i) {
        int k = i;
        for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
        Action swap = actions[i]; actions[i] = actions[k]; actions[k] = swap;
        State next = apply(s, actions[i]); Action ignored;
        value = -search(&next, depth - 1, -beta, -alpha, &ignored);
        if (stopped) return 0;
        if (value > best) { best = value; best_action = actions[i]; }
        if (value > alpha) alpha = value;
        if (alpha >= beta) { ++cutoffs; break; }
    }
    *chosen = best_action;
    *entry = (Entry){ key, depth, best, best <= original_alpha ? 2 : best >= original_beta ? 1 : 0, best_action };
    return best;
}
static Action choose(const State *s, int *completed_depth, int *advantage) {
    deadline = now_ns() + (uint64_t)BUDGET_MS * 1000000ULL;
    nodes = cutoffs = hits = 0; stopped = 0;
    Action best = {WAIT, 0, 0, 0};
    *completed_depth = 0; *advantage = evaluate(s);
    for (int depth = 1; depth <= 32 && depth <= s->remaining; ++depth) {
        Action candidate = best;
        int value = search(s, depth, -INF, INF, &candidate);
        if (stopped) break;
        best = candidate; *completed_depth = depth; *advantage = value;
        if (abs(value) >= WIN || now_ns() >= deadline) break;
    }
    return best;
}
#ifndef BOT_TEST
int main(void) {
    int me, width, height;
    if (scanf("%d%d%d", &me, &width, &height) != 3 || width != W || height != H || me < 0 || me > 1) return 1;
    for (int y = 0; y < H; ++y) {
        char row[W + 1];
        if (scanf("%13s", row) != 1 || strlen(row) != W) return 1;
        for (int x = 0; x < W; ++x) wall[y * W + x] = row[x] != '.';
    }
    initialize();
    int count, turn = me;
    while (scanf("%d", &count) == 1) {
        if (count < 0 || count > UNITS) return 1;
        State s = {0}; s.side = me; s.remaining = 150 - turn;
        for (int i = 0; i < count; ++i) {
            int id, type, hp, x, y, owner;
            if (scanf("%d%d%d%d%d%d", &id, &type, &hp, &x, &y, &owner) != 6) return 1;
            if (id < 0 || id >= UNITS || type < 0 || type > 1 || hp < 1 || hp > 10 || x < 0 || x >= W || y < 0 || y >= H || owner < 0 || owner > 2) return 1;
            s.u[id] = (Unit){y * W + x, hp, owner, type};
        }
        int depth, advantage; Action a = choose(&s, &depth, &advantage);
        if (a.kind == WAIT) puts("WAIT");
        else if (a.kind == MOVE) printf("%d MOVE %d %d\n", a.unit, a.target % W, a.target / W);
        else printf("%d %s %d\n", a.unit, a.kind == SHOOT ? "SHOOT" : "CONVERT", a.target);
        fflush(stdout);
        fprintf(stderr, "depth=%d advantage=%d nodes=%llu cutoffs=%llu tt=%llu\n", depth, advantage, (unsigned long long)nodes, (unsigned long long)cutoffs, (unsigned long long)hits);
        turn += 2;
    }
    return 0;
}
#endif
