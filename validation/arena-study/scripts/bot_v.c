#define _POSIX_C_SOURCE 200809L
/* CodinGame controls compiler flags; keep hot code optimized there too. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LEADERB
#define LEADERB 420
#endif
#ifndef LATEK
#define LATEK 0
#endif
#ifndef HPW
#define HPW 18
#endif
#define W 13
#define H 7
#define CELLS (W * H)
#define UNITS 14
#define ACTIONS (1 + UNITS * (4 + UNITS))
#define INF 10000000
#define WIN 1000000
#define TT_SIZE 65536
#ifndef BUDGET_MS
#define BUDGET_MS 49
#endif

typedef struct { uint8_t pos, hp, owner, leader; } Unit;
typedef struct { uint64_t lo, hi; } Bits;
typedef struct {
    Unit u[UNITS];
    uint32_t rng;
    uint16_t neutral_mask;
    uint8_t side, rng_known;
    int remaining, ready;
    uint64_t key, metadata;
    Bits occupied;
    uint16_t owned[3], leaders;
    uint64_t occupants[6];
} State;
enum { WAIT, MOVE, SHOOT, CONVERT };
typedef struct { int kind, unit, target, order; } Action;
typedef struct { uint64_t key; int depth, value, flag; Action best; } Entry;
typedef struct { unsigned char length, cell[CELLS]; } Ray;
static int wall[CELLS], distance_table[CELLS][CELLS];
static Ray rays[CELLS][CELLS];
static Bits ray_bits[CELLS][CELLS];
static uint8_t taxi[CELLS][CELLS], proximity[CELLS][CELLS];
static uint8_t leader_bonus[CELLS], fighter_bonus[CELLS];
static int adjacent[CELLS][4];
static uint64_t zobrist_pos[UNITS][CELLS], zobrist_attr[UNITS][11][3][2];
static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
static void bit_toggle(Bits *b, int p) {
    if (p < 64) b->lo ^= 1ULL << p; else b->hi ^= 1ULL << (p - 64);
}
static int bit_has(Bits b, int p) {
    return p < 64 ? (int)((b.lo >> p) & 1) : (int)((b.hi >> (p - 64)) & 1);
}
static uint64_t unit_key(int i, Unit u) {
    return u.hp ? (zobrist_pos[i][u.pos] ^ zobrist_attr[i][u.hp][u.owner][u.leader]) : 0;
}
static uint64_t metadata_key(const State *s) {
    return mix64((uint64_t)s->neutral_mask | ((uint64_t)s->remaining << 14)
        | ((uint64_t)s->side << 22) | ((uint64_t)s->rng_known << 23)
        | ((uint64_t)(s->rng_known ? s->rng : 0) << 24));
}
static uint64_t hash_state(const State *s) {
    uint64_t h = metadata_key(s);
    for (int i = 0; i < UNITS; ++i) h ^= unit_key(i, s->u[i]);
    return h;
}
static void prepare(State *s) {
    s->occupied = (Bits){0,0}; s->leaders = 0;
    memset(s->occupants,0,sizeof s->occupants);
    s->owned[0] = s->owned[1] = s->owned[2] = 0;
    for (int i = 0; i < UNITS; ++i) if (s->u[i].hp) {
        bit_toggle(&s->occupied, s->u[i].pos);
        s->occupants[s->u[i].pos >> 4] |= (uint64_t)(i+1) << ((s->u[i].pos & 15)*4);
        s->owned[s->u[i].owner] |= 1u << i;
        if (s->u[i].leader) s->leaders |= 1u << i;
    }
    s->metadata = metadata_key(s); s->key = hash_state(s); s->ready = 1;
}
static void replace_unit(State *s, int i, Unit u) {
    Unit old = s->u[i];
    if (s->ready) {
        s->key ^= unit_key(i, old) ^ unit_key(i, u);
        if (old.pos != u.pos || !old.hp || !u.hp) {
            if (old.hp) {
                bit_toggle(&s->occupied, old.pos);
                s->occupants[old.pos >> 4] &= ~(15ULL << ((old.pos & 15) * 4));
            }
            if (u.hp) {
                bit_toggle(&s->occupied, u.pos);
                s->occupants[u.pos >> 4] |= (uint64_t)(i + 1) << ((u.pos & 15) * 4);
            }
        }
        if (old.owner != u.owner || !old.hp || !u.hp) {
            if (old.hp) s->owned[old.owner] &= ~(1u << i);
            if (u.hp) s->owned[u.owner] |= 1u << i;
        }
        if (old.leader != u.leader || !old.hp || !u.hp) {
            s->leaders &= ~(1u << i);
            if (u.hp && u.leader) s->leaders |= 1u << i;
        }
    }
    s->u[i] = u;
}

static Entry tt[TT_SIZE];
typedef struct { uint64_t key; int value; uint8_t depth, flag; } QEntry;
static QEntry qtt[TT_SIZE];
typedef struct { uint64_t key; int value, valid; } EvalEntry;
static EvalEntry eval_tt[TT_SIZE];
static uint64_t deadline, turn_deadline, nodes, cutoffs, hits;
static int stopped;
static int use_futility = 1;
/* Bounded, role-aware quiet history; conversions may change unit ownership. */
static int16_t history[2][2][CELLS][CELLS];
static int use_history = 1;
static int use_threat_q = 1;
static void update_history(const State *s, Action a, int bonus) {
    if (a.kind != MOVE) return;
    int16_t *h = &history[s->side][s->u[a.unit].leader][s->u[a.unit].pos][a.target];
    *h += bonus - *h * abs(bonus) / 2048;
}
static uint64_t qnodes, pruned;

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
    memset(tt,0,sizeof tt); memset(qtt,0,sizeof qtt); memset(eval_tt,0,sizeof eval_tt);
    memset(history, 0, sizeof history);
    uint64_t seed = 0x123456789abcdef0ULL;
    for(int i=0;i<UNITS;++i) {
        for(int p=0;p<CELLS;++p) zobrist_pos[i][p]=mix64(seed += 0x9e3779b97f4a7c15ULL);
        for(int hp=0;hp<11;++hp) for(int owner=0;owner<3;++owner) for(int leader=0;leader<2;++leader)
            zobrist_attr[i][hp][owner][leader]=mix64(seed += 0x9e3779b97f4a7c15ULL);
    }
    for (int p = 0; p < CELLS; ++p) {
        leader_bonus[p] = 240 / (p + 1);
        fighter_bonus[p] = 90 / (p + 1);
        for (int d = 0; d < 4; ++d) adjacent[p][d] = neighbor(p,d);
    }
    for (int a = 0; a < CELLS; ++a) {
        for (int b = 0; b < CELLS; ++b) {
            Ray *r = &rays[a][b]; r->length = 0;
            ray_bits[a][b] = (Bits){0,0}; taxi[a][b] = manhattan(a,b);
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
            int blocked = 0;
            for (int i = 1; i < n; ++i) {
                int p = path[forward ? i : n - 1 - i];
                r->cell[r->length++] = p;
                blocked |= wall[p];
                if (!blocked) bit_toggle(&ray_bits[a][b], p);
            }
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
    for (int a = 0; a < CELLS; ++a) for (int b = 0; b < CELLS; ++b)
        proximity[a][b] = 100 / (1 + distance_table[a][b]);
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
static int fast_hit(const State *s, int from, int to) {
    Bits r = ray_bits[from][to];
    uint64_t lo = r.lo & s->occupied.lo;
    uint64_t hi = r.hi & s->occupied.hi;
    if (!(lo | hi)) return -1;
    int p;
    if (!hi && !(lo & (lo - 1))) p = __builtin_ctzll(lo);
    else if (!lo && !(hi & (hi - 1))) p = 64 + __builtin_ctzll(hi);
    else {
        /* Shallow negative slopes are not monotonic in linear cell index. */
        const Ray *path = &rays[from][to];
        int k = 0;
        while (!bit_has((Bits){lo,hi},path->cell[k])) ++k;
        p = path->cell[k];
    }
    return (int)((s->occupants[p >> 4] >> ((p & 15)*4)) & 15) - 1;
}
static int terminal(const State *s, int *value) {
    if (s->ready && s->owned[0] && s->owned[1] && s->remaining > 0) return 0;
    int count[2] = {0, 0};
    if (s->ready) {
        count[0] = __builtin_popcount(s->owned[0]);
        count[1] = __builtin_popcount(s->owned[1]);
    } else for (int i = 0; i < UNITS; ++i)
        if (s->u[i].hp > 0 && s->u[i].owner < 2) ++count[s->u[i].owner];
    if (!count[0] || !count[1] || s->remaining <= 0) {
        int diff = count[s->side] - count[1 - s->side];
        *value = diff > 0 ? WIN : diff < 0 ? -WIN : 0;
        return 1;
    }
    return 0;
}
static int evaluate_live(const State *s) {
    uint64_t key = s->key ^ s->metadata ^ (LATEK ? mix64(0xabcdefULL + (uint64_t)s->remaining) : 0);
    EvalEntry *entry = &eval_tt[key & (TT_SIZE - 1)];
    if (entry->valid && entry->key == key) return s->side ? -entry->value : entry->value;
    int score[2] = {0,0};
    for (int side = 0; side < 2; ++side) {
        unsigned own = s->owned[side];
        while (own) {
            int i = __builtin_ctz(own); own &= own - 1;
            const Unit *u = &s->u[i]; int nearest = CELLS;
            score[side] += 1000 + HPW * u->hp + (u->leader ? LEADERB : 0) + (LATEK * (150 - s->remaining)) / 150;
            unsigned targets = s->owned[1-side];
            if (u->leader) targets = (targets | s->owned[2]) & ~s->leaders;
            while (targets) {
                int j = __builtin_ctz(targets); targets &= targets - 1;
                int p = s->u[j].pos, dist = distance_table[u->pos][p];
                if (dist < nearest) nearest = dist;
                if (u->leader) score[side] += proximity[u->pos][p];
            }
            if (nearest < CELLS) score[side] += u->leader ? leader_bonus[nearest] : fighter_bonus[nearest];
        }
    }
    int value = score[0] - score[1];
    *entry = (EvalEntry){key,value,1};
    return s->side ? -value : value;
}
static int evaluate(const State *s) {
    int value;
    if (terminal(s, &value)) return value;
    State copy;
    if (!s->ready) { copy = *s; prepare(&copy); s = &copy; }
    return evaluate_live(s);
}
static int generate_mode(const State *s, Action actions[ACTIONS], int tactical) {
    State copy;
    if (!s->ready) { copy = *s; prepare(&copy); s = &copy; }
    int n = 0;
    if (!tactical) actions[n++] = (Action){WAIT, 0, 0, -100};
    unsigned own = s->owned[s->side];
    unsigned live = s->owned[0] | s->owned[1] | s->owned[2];
    while (own) {
        int i = __builtin_ctz(own); own &= own - 1;
        const Unit *u = &s->u[i];
        for (int d = 0; !tactical && d < 4; ++d) {
            int p = adjacent[u->pos][d];
            if (p >= 0 && !wall[p] && !bit_has(s->occupied,p))
                actions[n++] = (Action){MOVE, i, p, 0};
        }
        unsigned seen = 0;
        unsigned targets = u->leader ? (s->owned[1-s->side] | s->owned[2]) & ~s->leaders : live & ~(1u << i);
        while (targets) {
            int j = __builtin_ctz(targets); targets &= targets - 1;
            const Unit *v = &s->u[j];
            int dist = taxi[u->pos][v->pos];
            if (u->leader) {
                if (!v->leader && v->owner != s->side && dist == 1)
                    actions[n++] = (Action){CONVERT, i, j, v->owner == 2 ? 2000 : 4000};
            } else if (dist <= 6) {
                int hit = fast_hit(s, u->pos, v->pos);
                if (hit < 0 || (seen & (1u << hit))) continue;
                seen |= 1u << hit;
                const Unit *victim = &s->u[hit];
                int damage = 7 - taxi[u->pos][victim->pos];
                int order = damage * 40 + (damage >= victim->hp ? 1500 : 0);
                if (victim->owner == s->side) order = -order;
                if (victim->owner == 2) order /= 4;
                if (!tactical || order > 0) actions[n++] = (Action){SHOOT, i, j, order};
            }
        }
    }
    return n;
}
static int generate(const State *s, Action actions[ACTIONS]) { return generate_mode(s, actions, 0); }
static unsigned random_next(State *s, unsigned bound) {
    s->rng = (s->rng * 1103515245u + 12345u) & 2147483647u;
    return s->rng % bound;
}
/* Dead neutrals remain in Java's neutral list. A freshly killed unit still
   blocks its tile until AFTER this phase. Trapped live neutrals consume a draw. */
static void move_neutral(State *s, int corpse) {
    if (!s->rng_known || !s->neutral_mask) return;
    unsigned index = random_next(s, 12);
    int id = -1;
    for (int i = 0; i < UNITS; ++i) if (s->neutral_mask & (1u << i)) {
        if (!index--) { id = i; break; }
    }
    if (id < 0 || s->u[id].hp <= 0) return;
    int cells[4], n = 0;
    Bits occupied = s->occupied;
    if (!s->ready) { occupied = (Bits){0,0};
        for (int i=0;i<UNITS;++i) if(s->u[i].hp) bit_toggle(&occupied,s->u[i].pos); }
    if (corpse >= 0) bit_toggle(&occupied, s->u[corpse].pos);
    for (int d = 0; d < 4; ++d) {
        int p = adjacent[s->u[id].pos][d];
        if (p >= 0 && !wall[p] && !bit_has(occupied,p)) cells[n++] = p;
    }
    unsigned choice = random_next(s, n ? (unsigned)n : 1u);
    if (n) { Unit u = s->u[id]; u.pos = cells[choice]; replace_unit(s,id,u); }
}
static State apply(const State *s, Action a) {
    State next = *s;
    int corpse = -1;
    if (a.kind == MOVE) { Unit u = next.u[a.unit]; u.pos = a.target; replace_unit(&next,a.unit,u); }
    else if (a.kind == CONVERT) {
        Unit u = next.u[a.target]; u.owner = s->side; replace_unit(&next,a.target,u);
        next.neutral_mask &= ~(1u << a.target);
    }
    else if (a.kind == SHOOT) {
        int hit;
        if (s->ready) hit = fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos);
        else { int occ[CELLS]; occupancy(s,occ); hit = bullet_hit(s->u[a.unit].pos,s->u[a.target].pos,occ); }
        if (hit >= 0) {
            Unit u = next.u[hit];
            int hp = u.hp - (7 - taxi[s->u[a.unit].pos][u.pos]);
            u.hp = hp > 0 ? hp : 0;
            if (!u.hp) corpse = hit;
            replace_unit(&next,hit,u);
        }
    }
    move_neutral(&next, corpse);
    next.side ^= 1; --next.remaining;
    if (next.ready) {
        next.metadata = metadata_key(&next);
        next.key ^= s->metadata ^ next.metadata;
    }
    return next;
}
static int same(Action a, Action b) { return a.kind == b.kind && a.unit == b.unit && a.target == b.target; }
static int check_time(void) {
    ++nodes;
    if ((nodes & 31u) == 0 && now_ns() >= deadline) stopped = 1;
    return stopped;
}
/* Immediate material threats include indirect shots and enemy conversion. */
static int material_threat(const State *s) {
    unsigned own = s->owned[s->side], enemy = s->owned[1-s->side];
    unsigned leaders = enemy & s->leaders;
    while (leaders) {
        int i = __builtin_ctz(leaders); leaders &= leaders - 1;
        unsigned targets = own & ~s->leaders;
        while (targets) {
            int j = __builtin_ctz(targets); targets &= targets - 1;
            if (taxi[s->u[i].pos][s->u[j].pos] == 1) return 1;
        }
    }
    unsigned fragile = 0, targets = own;
    while (targets) {
        int j = __builtin_ctz(targets); targets &= targets - 1;
        if (s->u[j].hp <= 6) fragile |= 1u << j;
    }
    if (!fragile) return 0;
    unsigned shooters = enemy & ~s->leaders;
    unsigned live = s->owned[0] | s->owned[1] | s->owned[2];
    while (shooters) {
        int i = __builtin_ctz(shooters); shooters &= shooters - 1;
        /* If no victim is close enough for lethal damage, no aim can help. */
        unsigned victims = 0; targets = fragile;
        while (targets) {
            int j = __builtin_ctz(targets); targets &= targets - 1;
            if (s->u[j].hp <= 7 - taxi[s->u[i].pos][s->u[j].pos]) victims |= 1u << j;
        }
        if (!victims) continue;
        targets = live & ~(1u << i);
        while (targets) {
            int j = __builtin_ctz(targets); targets &= targets - 1;
            if (taxi[s->u[i].pos][s->u[j].pos] > 6) continue;
            int hit = fast_hit(s,s->u[i].pos,s->u[j].pos);
            if (hit >= 0 && (victims & (1u << hit))) return 1;
        }
    }
    return 0;
}
/* Bounded tactical extension. Stand-pat is a heuristic, not a legal pass:
   neutral movement and zugzwang mean this is intentionally selective. */
#ifndef Q_LAZY_EVAL
#define Q_LAZY_EVAL 1
#endif
static int quiescence(const State *s, int alpha, int beta, int left) {
    State copy;
    if (!s->ready) { copy = *s; prepare(&copy); s = &copy; }
    ++qnodes;
    if (check_time()) return 0;
    int value;
    if (terminal(s, &value)) return value;
    int original_alpha = alpha, original_beta = beta;
    QEntry *entry = &qtt[s->key & (TT_SIZE - 1)];
    if (entry->key == s->key && entry->depth == left + 1) {
        if (entry->flag == 0 || (entry->flag == 1 && entry->value >= beta)
                || (entry->flag == 2 && entry->value <= alpha)) {
            ++hits; return entry->value;
        }
    }
    int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));
    /* Threatened nodes cannot stand pat: their static value is discarded. */
    int best = (Q_LAZY_EVAL && unstable) ? -INF : evaluate_live(s);
    if (!left || (!unstable && best >= beta)) {
        *entry = (QEntry){s->key,best,left+1,!left ? 0 : 1};
        return best;
    }
    if (unstable) best = -INF;
    if (best > alpha) alpha = best;
    Action actions[ACTIONS]; int n = generate_mode(s, actions, !unstable);
    for (int i = 0; i < n; ++i) {
        int k = i;
        for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
        Action tmp = actions[i]; actions[i] = actions[k]; actions[k] = tmp;
        Action a = actions[i];
        /* Under threat, search actual replies, including escapes/blockers/WAIT.
           No stand-pat: declining the opponent's material gain is not legal. */
        if (!unstable && a.kind == SHOOT && a.order <= 0) continue;
        State next = apply(s, a);
        value = -quiescence(&next, -beta, -alpha, left - 1);
        if (stopped) return 0;
        if (value > best) best = value;
        if (value > alpha) alpha = value;
        if (alpha >= beta) { ++cutoffs; break; }
    }
    *entry = (QEntry){s->key,best,left+1,best <= original_alpha ? 2 : best >= original_beta ? 1 : 0};
    return best;
}
static int search(const State *s, int depth, int alpha, int beta, Action *chosen) {
    if (!depth) return quiescence(s, alpha, beta, 4);
    if (check_time()) return 0;
    State copy;
    if (!s->ready) { copy = *s; prepare(&copy); s = &copy; }
    int value;
    if (terminal(s, &value)) return value;
    uint64_t key = s->key;
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
    for (int i = 0; i < n; ++i) {
        Action a = actions[i];
        /* Keep hostile tactics ahead of quiet history; hash move always first. */
        if (use_history) {
            actions[i].order *= 4096;
            if (a.kind == MOVE)
                actions[i].order = history[s->side][s->u[a.unit].leader][s->u[a.unit].pos][a.target];
            if (a.kind == WAIT) actions[i].order = -4096;
        }
        if (same(a, hint)) actions[i].order += 100000000;
    }
    int best = -INF; Action best_action = actions[0];
    int selective = use_futility && depth == 1 && beta == alpha + 1
        && s->remaining > 4 && abs(alpha) < WIN / 2;
    int static_value = selective ? evaluate_live(s) : 0;
    Action quiets[UNITS * 4]; int quiet_count = 0;
    for (int i = 0; i < n; ++i) {
        int k = i;
        for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
        Action swap = actions[i]; actions[i] = actions[k]; actions[k] = swap;
        /* Only late quiet cultist moves in null-window nodes; never prune
           leaders, tactical actions, the hash move, or the first candidate. */
        if (selective && i > 0 && actions[i].kind == MOVE
                && !s->u[actions[i].unit].leader && !same(actions[i], hint)
                && static_value + 300 < alpha) { ++pruned; continue; }
        State next = apply(s, actions[i]); Action ignored;
        if (i == 0) value = -search(&next, depth - 1, -beta, -alpha, &ignored);
        else {
            value = -search(&next, depth - 1, -alpha - 1, -alpha, &ignored);
            if (!stopped && value > alpha && value < beta)
                value = -search(&next, depth - 1, -beta, -alpha, &ignored);
        }
        if (stopped) return 0;
        if (value > best) { best = value; best_action = actions[i]; }
        if (value > alpha) alpha = value;
        if (alpha >= beta) {
            ++cutoffs;
            if (use_history && actions[i].kind == MOVE) {
                int bonus = depth * depth * 16;
                if (bonus > 256) bonus = 256;
                update_history(s, actions[i], bonus);
                for (int j = 0; j < quiet_count; ++j) update_history(s, quiets[j], -bonus);
            }
            break;
        }
        if (actions[i].kind == MOVE) quiets[quiet_count++] = actions[i];
    }
    *chosen = best_action;
    *entry = (Entry){ key, depth, best, best <= original_alpha ? 2 : best >= original_beta ? 1 : 0, best_action };
    return best;
}
static Action choose(const State *s, int *completed_depth, int *advantage) {
    State root = *s; prepare(&root); s = &root;
    deadline = turn_deadline ? turn_deadline : now_ns() + (uint64_t)BUDGET_MS * 1000000ULL;
    nodes = cutoffs = hits = qnodes = pruned = 0; stopped = 0;
    Action best = {WAIT, 0, 0, 0};
    *completed_depth = 0; *advantage = evaluate(s);
    for (int depth = 1; depth <= 32 && depth <= s->remaining; ++depth) {
        Action candidate = best;
        int window = depth >= 3 ? 100 : INF;
        int value;
        for (;;) {
            int alpha = window == INF ? -INF : *advantage - window;
            int beta = window == INF ? INF : *advantage + window;
            value = search(s, depth, alpha, beta, &candidate);
            if (stopped || (value > alpha && value < beta) || window == INF) break;
            window = window >= 1600 ? INF : window * 4;
        }
        if (stopped) break;
        best = candidate; *completed_depth = depth; *advantage = value;
        if (abs(value) >= WIN || now_ns() >= deadline) break;
    }
    return best;
}
/* Infer hidden RNG by matching all legal opponent replies to observed input.
   Retain ambiguity instead of silently asserting a guessed seed is exact. */
#define RNG_CANDIDATES 64
typedef struct { State states[RNG_CANDIDATES]; int n; } Tracker;
static int same_board(const State *a, const State *b) {
    for (int i = 0; i < UNITS; ++i) {
        const Unit *u = &a->u[i], *v = &b->u[i];
        if (u->hp != v->hp) return 0;
        if (u->hp && (u->pos != v->pos || u->owner != v->owner || u->leader != v->leader)) return 0;
    }
    return 1;
}
static void track_observation(Tracker *t, State *observed, int first) {
    Tracker next = {0};
    if (first) {
        observed->neutral_mask = ((1u << UNITS) - 1) & ~3u;
        for (int i = 2; i < UNITS; ++i)
            if (observed->u[i].hp && observed->u[i].owner != 2)
                observed->neutral_mask &= ~(1u << i);
        observed->rng = 42; observed->rng_known = 1;
        if (observed->side) {
            /* Player zero's first action consumes one or two RNG draws. */
            random_next(observed, 12);
            next.states[next.n++] = *observed;
            random_next(observed, 1);
        }
        next.states[next.n++] = *observed;
    } else {
        for (int k = 0; k < t->n; ++k) {
            Action actions[ACTIONS]; int n = generate(&t->states[k], actions);
            for (int i = 0; i < n; ++i) {
                State reply = apply(&t->states[k], actions[i]);
                if (!same_board(&reply, observed)) continue;
                int duplicate = 0;
                for (int j = 0; j < next.n; ++j)
                    if (next.states[j].rng == reply.rng && next.states[j].neutral_mask == reply.neutral_mask) duplicate = 1;
                if (!duplicate) {
                    if (next.n == RNG_CANDIDATES) { next.n = 0; goto done; }
                    next.states[next.n++] = reply;
                }
            }
        }
    }
done:
    *t = next;
    observed->rng_known = t->n == 1;
    if (t->n) {
        observed->rng = t->states[0].rng;
        observed->neutral_mask = t->states[0].neutral_mask;
    }
}
static void track_action(Tracker *t, Action a) {
    for (int i = 0; i < t->n; ++i) t->states[i] = apply(&t->states[i], a);
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
    int count, turn = me; Tracker tracker = {0};
    while (scanf("%d", &count) == 1) {
        if (count < 0 || count > UNITS) return 1;
        uint64_t turn_started = now_ns();
        turn_deadline = turn_started + (uint64_t)BUDGET_MS * 1000000ULL;
        State s = {0}; s.side = me; s.remaining = 150 - turn;
        for (int i = 0; i < count; ++i) {
            int id, type, hp, x, y, owner;
            if (scanf("%d%d%d%d%d%d", &id, &type, &hp, &x, &y, &owner) != 6) return 1;
            if (id < 0 || id >= UNITS || type < 0 || type > 1 || hp < 1 || hp > 10 || x < 0 || x >= W || y < 0 || y >= H || owner < 0 || owner > 2) return 1;
            s.u[id] = (Unit){y * W + x, hp, owner, type};
        }
        track_observation(&tracker, &s, turn == me);
        int depth, advantage; Action a = choose(&s, &depth, &advantage);
        track_action(&tracker, a);
        if (a.kind == WAIT) puts("WAIT");
        else if (a.kind == MOVE) printf("%d MOVE %d %d\n", a.unit, a.target % W, a.target / W);
        else printf("%d %s %d\n", a.unit, a.kind == SHOOT ? "SHOOT" : "CONVERT", a.target);
        fflush(stdout);
        fprintf(stderr, "depth=%d advantage=%d nodes=%llu cutoffs=%llu tt=%llu qnodes=%llu pruned=%llu rng=%d elapsed_us=%llu\n", depth, advantage, (unsigned long long)nodes, (unsigned long long)cutoffs, (unsigned long long)hits, (unsigned long long)qnodes, (unsigned long long)pruned, tracker.n, (unsigned long long)((now_ns() - turn_started) / 1000));
        turn += 2;
    }
    return 0;
}
#endif
