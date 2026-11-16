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
"HjJdNX0mmjOjoPmxdjoJufYrtjTBMkEyPTDIs+4pOq0fsx+zoKbhtaqz1jdEKku0V6mWtTIweShzIOOsAi1bv3IyyTVqN68jb7puMJ46GTSHLZu4TDWXIWEy"
"QakkMXmoDjOisESovjUJrK60ZL+yKHov67uXwEyn06qzssApZTxNOyy5u7bzPSIm/aRCqVUyQ7LFLoC2l75bOFc1zzS9K6uwhTYvw6kxUjy3rWc3LaystFLE"
"e7yMtmCuQbeVKhy2mrQHsjS7kzV/sK64OKctvJowxCxEtS0u2TYzrQAyFDMqNziyBqvQMrgsXTS7NYgvbbRxMwujIUCuJ18zRLgKOI80/jnWsBgxRjJ8P4xA"
"S7Xxui60bsEauWKpG6oZKwq1BrxgsZq33CwStvynZ7bNNvosBbp2o1c7Lbcfshwsh7QCMew2Vbp2sbM0Mi6Aua4sKrgLuAq5CcCkMji4ka7TNTgn47K8t7s0"
"YihqOuE8c6zRKlU4rsCPN303+7vesVI9aS/juw4wcTnnKTy4bbSwLOq4QzYlNLqtup3et508U7AOLpm22bgjOeQ0ZzawuGq4pjNSv6E8l7I7s8A8v7jLtEO3"
"8Txytvg9JjyKt/I2qbKBtFIwX7gQKr093Dv4uiM/hLgQtxgsR7VxNj83JbcBNJI5TjiSKww0ubSBtEs6acDTOje4S73uNdWwUzvlIPIsijZ5to63JDgcuta5"
"EKQppKm1TrI7tpC6ECalOs23rqu/JzQ8GjmKuBit0bLpMO+4vbXBNckoHjykNWEpjTzutsuuybB3tUy6oq5XOHasr7gIGxm4frIFOAK2Ibh1sbG1FK0QqcbB"
"EzgouAO5wL0PNB02qzj8NgU98DhaNTs8M6t6PPQ00i/ysS47TTYSMNAyXzsLtwAzFb1pNJS0OTgWNH+zsra+qi464y7qr0MnGDRONO685bsiwNs0DLdlub28"
"QTQpL2C0JLgQQd+yNywnKSmyKqpmtbk3hzh+KhO39bJDtJY5hzDcNQI4qrqUrNW85rXEMOS0cD/6srC4fCzYN6E+NDjUtVm8F7Sgmp+jH7S0JPqeri/INu6y"
"faxUt1+4VDK6suqg1KvxuJG8NrqQMLCxU6x9u2OwhLY3Mb+xga/yJ068WDjUsvqzULbtsz25C6V4sXepxKN0uqiyDLhsngi6XbF0tjmwRzQGvSUr7q4orV+w"
"ijLzt8m49qjxrnG6FLXfuWyvb64EuKWmxKK+tIAoRbFsL5u08jfxLUSxBjwIND4yLp1TMOezAbrir08wYzSgLZawczMrr0owaDatLMmbGK46NJU0gbJYMBk0"
"Ua/tI26v9y9nM2uxbzdZL3WkLrF9Mi42zy62M3kwZjSkqpqwrjPSLk0ygrMKJRQtbrM+M5awfRUmsCI6DzAUMNk2izqHtJwvDa/3Sg==";
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
static struct{uint64_t key;int valid,r[ACTIONS];} nc[64];
static void nn_begin(const State *s){memset(nc,0,sizeof nc);
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
    int ci=-1;if(NN_ENABLE&&nn_root&&nn_root->remaining-s->remaining==1&&depth>=3){ci=key&63;if(!nc[ci].valid||nc[ci].key!=key){nc[ci].valid=1;nc[ci].key=key;for(int j=0;j<n;j++){if(now_ns()>=deadline){stopped=1;return 0;}float f[NF];features(s,actions[j],f);nc[ci].r[j]=(int)(nn_score(f)*1024);}}}
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
        else if(ci>=0)actions[i].order=nc[ci].r[i];
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
