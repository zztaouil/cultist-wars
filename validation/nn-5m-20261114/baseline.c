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
    uint64_t key = s->key ^ s->metadata;
    EvalEntry *entry = &eval_tt[key & (TT_SIZE - 1)];
    if (entry->valid && entry->key == key) return s->side ? -entry->value : entry->value;
    int score[2] = {0,0};
    for (int side = 0; side < 2; ++side) {
        unsigned own = s->owned[side];
        while (own) {
            int i = __builtin_ctz(own); own &= own - 1;
            const Unit *u = &s->u[i]; int nearest = CELLS;
            score[side] += 1000 + 18 * u->hp + (u->leader ? 420 : 0);
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
    int best = evaluate_live(s);
    int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));
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

#define NF 128
#ifndef NN_ENABLE
#define NN_ENABLE 1
#endif
static int victim(const State*s,Action a){return a.kind==SHOOT?fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos):-1;}
/* Fixed scaling; no fitted statistics. Unit slots retain referee IDs. */
static void features(const State*s,Action a,float*f){
 memset(f,0,NF*sizeof(float));int src=a.kind?s->u[a.unit].pos:45;
 int hit=victim(s,a);int dst=a.kind==MOVE?a.target: a.kind==CONVERT?s->u[a.target].pos:hit>=0?s->u[hit].pos:src;
 int k=0;for(int i=0;i<14;i++){Unit u=s->u[i];if(u.hp){f[k]=(u.pos%13-dst%13)/12.f;f[k+1]=(u.pos/13-dst/13)/6.f;f[k+2]=u.hp/10.f;f[k+3]=u.owner==s->side;f[k+4]=u.owner==1-s->side;f[k+5]=u.owner==2;f[k+6]=u.leader;}k+=7;}
 f[98+a.kind]=1;f[102]=src%13/12.f;f[103]=src/13/6.f;f[104]=dst%13/12.f;f[105]=dst/13/6.f;
 f[106]=a.kind?s->u[a.unit].hp/10.f:0;f[107]=a.kind?s->u[a.unit].leader:0;
 f[108]=s->remaining/150.f;f[109]=a.order/4000.f;
 if(hit>=0){f[110]=(7-taxi[src][dst])/7.f;f[111]=(7-taxi[src][dst])>=s->u[hit].hp;f[112]=s->u[hit].owner==s->side;f[113]=s->u[hit].owner==2;}
 for(int group=0;group<3;group++){int owner=group==0?s->side:group==1?1-s->side:2;int count=0,hp=0,near=91,leader=91;
 for(int i=0;i<14;i++)if(s->u[i].hp&&s->u[i].owner==owner){count++;hp+=s->u[i].hp;int d=distance_table[dst][s->u[i].pos];if(i!=a.unit||!a.kind){if(d<near)near=d;}if(s->u[i].leader&&d<leader)leader=d;}
 f[114+group*4]=count/14.f;f[115+group*4]=hp/140.f;f[116+group*4]=near/91.f;f[117+group*4]=leader/91.f;}
 f[126]=distance_table[src][dst]/91.f;f[127]=s->side;
}
static const char nn_blob[]=
"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"
"v66LKKC1YzICLbC0PbLnLJsqV7L6KkCtI5/XLnawMK2rrs2xL7K4tawu87fyrqEo1K6tsHOuv6TPtEKtPrTrsDypka35r1K0/q8PtNSpz7FzsweyGCoarxi1"
"MrTYuSiuGCdyspKiFbSWr6OwOSstLw8xm6jYtlCvmrMbrkOwCDWEtlkyFbRbtZK1WS59rVEvH7C3IZW3a7MgscOvvzlptwSl7bU+vJC1gRnAuOu2yCj/sju0"
"lLJrtQyz/bMmtIQy8LTpKSIxbK/vtHK0mTJsp3C6VK6zJVmweqwwrqC2/Kwqr+y0XCfNLJyukK/ct5K1WrWIsoaz8rU2s4az/69GpFa0nrJLtrcwXzVUtkS4"
"J6+1rpyyvK9atGipZiVctFoQGDAcpk+u5Z9ssAu4/LLhtGCwEbCgKOWyVrazpIKv/rictS+uGyzjMMQywbdqtMqnD7cPsf2xAbXhI1knXCl5pvMk0awNKHyq"
"Naj/pQ6tdLBWJHSuZi0RJYqd661MrSQmS6g+KK6nAqxnpZus9yY2rdGeRKtCorOtuqaiK9uolajCKBkeYR0ZrU6vcCxgqTSkuyBwJZmnyystqlGveC3urr2p"
"mCR1KvkmUqe/KIAgmK0lomWfaaf/rqClnqylrhGw0ytpqXWuPSyMKA2hX55SrvGtXi3ureElDa+9qDuvdKiJJVwnVqg1prmqn6K7Jd+t3RHTIZeoe6J0rNms"
"Nq2WrxSvHiXHKIwp46mZqfSsRq3tKEmta6/roxqtY6uEKcetYCm8KPWlm6xZq3iwmamprhusjysGLOopjTBGOZQqa7VrrlwsZS2Eu2ktlzJgKAm8A6/8Nr63"
"h7ZKN1KuSj5At705s7ZuM4q5/a4HMUcqXq0+tRA1JTq7N1g4EaV0voYldLpjvWo5ETjttcg8QzfbvpIuqTQAMqA8a7jVtu44lja0L6s5BrWiMJixVi3zvgmz"
"O7D4O220l6tIN621ejgqtNeliEAsOiE4liqwtrg3NjFUKze9rjbmtgazUD3euMe0ublNNp831jhWuFE5j7DJNeK0z7WPHog1DLVJOv4wbzeQOk+3frd+Nbi1"
"Lz6ytDM4djV7MBgo8rGEt4qpfzSGuyc4FjsvvTe1TDxnLV07VjrKNXOpxLklMTU0CbzYuOI27is2slg8YC6quISleDFgPBGr2DRhN7qxnLYLqPC6nzw1tgAr"
"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";
static float nn_params[24833];
#define nn_weight0 (nn_params+0)
#define nn_bias0 (nn_params+16384)
#define nn_weight1 (nn_params+16512)
#define nn_bias1 (nn_params+24704)
#define nn_weight2 (nn_params+24768)
#define nn_bias2 (nn_params+24832)

static int nn_b64(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:63;}
static void nn_init(void){
 static int ready;if(ready)return;ready=1;
 unsigned acc=0, low=0;int bits=0,bytes=0,n=0;
 for(int i=0;nn_blob[i]&&nn_blob[i]!='=';i++){
  acc=(acc<<6)|(unsigned)nn_b64(nn_blob[i]);bits+=6;
  if(bits>=8){bits-=8;unsigned b=(acc>>bits)&255;
   if(!(bytes++&1))low=b;else{
    unsigned h=low|(b<<8),e=(h>>10)&31,m=h&1023;float v;
    if(e){unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(&v,&u,4);}
    else v=(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);
    nn_params[n++]=v;
   }
  }
 }
}
static float nn_score(const float *f){
 float h0[128],h1[64];
 for(int h=0;h<128;h++){float v=nn_bias0[h];for(int j=0;j<128;j++)v+=nn_weight0[h*128+j]*f[j];h0[h]=v>0?v:0;}
 for(int h=0;h<64;h++){float v=nn_bias1[h];for(int j=0;j<128;j++)v+=nn_weight1[h*128+j]*h0[j];h1[h]=v>0?v:0;}
 float out=nn_bias2[0];for(int j=0;j<64;j++)out+=nn_weight2[j]*h1[j];return out;
}
static const State *nn_root;
static Action nn_actions[ACTIONS];static int nn_ranks[ACTIONS],nn_count;
static void nn_begin(const State *s){
 nn_root=0;nn_count=0;if(!NN_ENABLE)return;
 nn_init();nn_count=generate(s,nn_actions);float scores[ACTIONS];
 for(int i=0;i<nn_count;i++){float f[NF];features(s,nn_actions[i],f);scores[i]=nn_score(f);}
 for(int i=0;i<nn_count;i++){int rank=0;for(int j=0;j<nn_count;j++)if(scores[i]>scores[j]||(scores[i]==scores[j]&&i<j))rank++;nn_ranks[i]=rank;}
 nn_root=s;
}
static int nn_order(Action a){for(int i=0;i<nn_count;i++)if(same(a,nn_actions[i]))return nn_ranks[i];return -1;}
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
        if (s == nn_root) actions[i].order = nn_order(a);
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
    nn_begin(s);
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
    nn_root = 0;
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
