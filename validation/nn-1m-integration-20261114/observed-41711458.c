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
"Z6Wxp3gqC635rHggeKQMnv6pGrO5rBahqqqJqBywyysQH3Yn9qMuLpWsSq0FKRCr2autLu4ub6sSsaEnOaaOKRyh7yw+LYosqi69LpcsRK11qx6pK5o3q0us"
"ijFwLQCtKqw1sJYsOi51KnYlPq7MF0AvC62wsCuw3Sg2qnIrkKryMoWwQ6lfrasr9KpZJWyux6l9p1Stla+9LOgu5aGVozqf4CSJqiYpYKcZKIWqlzBVstSt"
"iKZmMPKeCq2eqPUdVKrqqpax4yxBpXywf6y3qhQybyzeLick8q1Mq4wwrrXktJO1vqgpLY4jxR+hqYexHrQqLHupv6xAJSwsu622qX2oV6g2rHyu8C4crdEo"
"ESnxrt8uniwEppGse6OfpTQlS6mQLQSiKLE7q1qsei9WKxSkjynPl8KpDjDtKIuueK6Zqr+wIiuhHu0qYioyl2ev2aiWrBoo26vPqHuuBi8Hr8krsKxQKBko"
"Q7DeJ9gwDJuEKx2tta5iK++swyHJGgYtri9mqf6lpKyuLKksFCx/Kbwol7FZLp4u/ClGLA8wljHwMXirIC87pIUofyy8JX4s/zCaL6YoSS+GrxolEim6qjMs"
"Y60yrumtyCyeIRasnasVnKmotirRKNCtADFHKTwc4DP5oKKm963yrQOsRS7nLaYq4rBLG1IiAqZ9pHyiWijcLPOocyAUKy4sAin8qK4re56YK/khfC6eqEos"
"Bqueq4MoKCkcJA8pwaktLTcsZC2xpR8yaywBKaGhrht/pbyehSy5JrYsPi3qo+oXaqdwJmMmrCsSqqcnSKryrWKoHjDRohGpNSpnJtAskyrsH5os6iwPkFkq"
"OihwMECmwiw6MFSrMZ20KJAydCkPLPkrm6ezrTOhejIXp5Ylbp6rqK8pJSYUM+Aq2abZm5OraK4kLcQs9iOSpw6ftKVSqIQs+TBGMV4kqytjrAetPSiXLQGg"
"3qYZqj0jRqser3qvs6jKJXYxHi0Kru2oyqygqIowwTRcM0ylXSzPJ8E12SxOqm6lgCyAKXMuECyjrpGfrCwxJSQqSR+fqhAslyVmKXSotKtdquKvOTK7JOar"
"vBz5JD8cUSqnK4wu0CqLD7SiTi1GqFet7i4gp3mqVSRMHNUdWS8aMYuRvamYqgwhkLBApeco5KxoIngnWytDL08tohmjpC4vlCOQpW0pvjD1KbUmayUiK30p"
"f56cLvAtiKE3ncOpJalNrsIo2yypKTao1arWL5iopzDdLWSpbif5lhshHyQIMc6PQi0DL36svi6sq54tFy43rLQvqigDn4Ups55kFFagI6xZqaanGKw/rImq"
"u6tTMbMrqZ/jrsKm3a29Md4xDS7nrHYuqS2HNRysuCjUJ1awvKJuoaaorq2HrYsj4SVEJXyjWKUkrNmsA6EJKvIscioJLaKrrhUErSCiOqC4F2QnHaKmrZsn"
"xi4qpjguJawVlRauTyjqKgyk9Co1qROstiiiIKekiKWQMCopEqyHKuOhcSyCm3MYr55fq5OtA6YhIBamcShtHIitPKgIqnOdkitjJP8kX67nKTksBagBqMkj"
"AjCQrLAqAasipTWi1CofLD+uDR0mqzWswKnZpbkvp5wzomKta6arJSIo4izwLWYmDZ/apbOWKahuMm8wY6o/o2WkWyxus4Ay96rCtlayLzkMLy8pJyo6s7ye"
"/q2DMdE5ayz/qY2s4qmSrDEyd60ALIst2CwELQsoWyygLgAxmK2CJJ+ov7AGrI+fIqK7pWMvUSeGMIkuUJ1hJ9sraa2xr3Cpii/nKSAhcih3LR+wki0wMc0p"
"oCgTq7MsZ6wDLDcruipwpTkkhyeCsKklRzDfJzEq66kyrNKwRS5CJSqhRixUK3isnqjiGxOrcinLJs2sBi5NMOQtOKyoLN8t6CTXJtavQTOtrI+sCSxiIMKo"
"Di6dKWGt8aWFIEYp1i10nQcwma0Kqg8o8yPrLhOnijGWLCstSidLqxwgoKyFrLksc656LE0tDi5urFym8ap8Kzo0TqxFrGGz4y9PIzIo8zRBNDarCTBPKHM4"
"86nfqaiexC+uqIkp8ySxrpOnrbDFIoCpErCRrdkwnSDgJuCpLqXoJzst/SrjqooqeCjprV+qsy98paSnuxAMMHuiBS3ZMX+jlS3fKb2Yei51q5sq8K5LqG6o"
"wCrvLEwoniZro5ooFiSBLvAr0S22LIqlySriJiMtVScBK1emjy7Bqy8rDScClPosVaT3MYCnCCoErZaswi6OpeQv46d6qm0pE408K3opVJUdqGQs+ymmKAWl"
"bwwXLYalfyqZImuho6RErWSsyaiHo5Sts69iKLQk4jP0J/6tiRhbMa4m5SebLNWxsqZMMGWqV5UbqSKpyrACLmQlHDXdsAmh6S3irB2qQjIkrCmsYyyMsMiq"
"kabnrMIpzaxpKJsufzAerBAryaDor2yqG6woITAtaC1voxevurCYLGKo9TGOra8pRLFBKLkgaqk0K82lpCcGsfqqaaklJkmt8SrbLAGpQKrIGcsmYiedpGUj"
"vrByLPishq0TMm6klyzunLIvzh4ELDotLquWKKMu1CwtJlkwiDImoVeqU6z7Icytb6apMekvvaX1rEslTSx1rF0w4iO+KpQtmbHkpUmwPC9DJ8wXhKuvJywl"
"CDFGLiUj+iebrXcoohpEqGCy1KnxJpEijqq4MV6sVbF8sWwwLDV3KSKr06JxJm+ftS/nL1gwf6UBoOWqCzI7JswqoSuXro2ldSthJB4u26Twn4ytNjDQqDEv"
"NKv6nw+muqkhMVqtDrJlMCAtPKz0qFcrw6pOqAYu4SE4qk6M2qZ0MIQtazLTJAurH66BpGYr+rBtLSyVOqjCrX2uUoIUrEyrwiCxprixxKzrLJ0laCsSqQ8s"
"c6PMrc8lfSomrz0gm6YoKlynXq2lLMGn56yyqeCpfKxSrGUwCKuTqjIg3CrQGGCpMioGLFusgKljsHAlky6hqlChBCyHJMaq+S8+qVGtwaCUL6anv6VPqjuq"
"B63IsDgs7R4IrHAqKSENJ3+ygapEq6ouLadqFm6haqSGrVIVHCsIMVwlXaQwK8mqsymfMMkqigw8r1AhuSjZJOUZjao8IcirlagrIRqmTi2KLj4uMqbuJhws"
"qaVVrbYojC1AqoEkdS4sqSakT6x2MFKa+yxkoqqteifzG5QoByXqLZkS3SNRnsGl5y8Qp0OlmjAnKLinBq03LVas5yi9H2wfla0ZpCEpTxzMrJ4mujCMrPor"
"bzGUIlUhlSS9MPCpSyeXMGexLCx9LDeuxKunremnyCy0rCixRa9CLS4s0zAXIporf7ClNNQuo60Gp6EYSK2cqpQrdqScqX4x8KqJq6YegjE7oFweCKvYKvgp"
"bzCBsIa16aGpLjQsQShBJNUenKxCK00l5DNHLJScF6xDr82dFTGeq1ovpyo6LHatF6kKnMOpaSsiLJQyYCqOKcQsh6+WK7KqFigyoyMs56w3Ksewd6xFJyuu"
"vCTkMZ2kSKMDqgwwr6XprJIt0KlHKL0pIy+QqhQqTDAOKFkuaCCHKtudFSktLEatHKB5MH8l/SeKMYCocKhVJnYs0S2CKXEwuDDtnlYtMi6mLKIpkDGhMQUr"
"Iq1BIn2v5AvLMdKtlCrHrNuph6lPLO8uE60YpWAtx6hLLQ6saS+ysWYmvppurK0qVCmjMhGv86lWMFmmoybFoWguCCgLr2MmiSCUroM0QrDyoVuwbyiLJKOr"
"wi94LeCboS3CHg0mPTKGqDAppy1VqRomOK45NhKw2q/SISSsYi2HpB4oSyrtsHOpYCT5HoAteDRJKbqqXiRYnrktBhzMKAUxtZ9eMBWtXyxNK/gxTqVHoIgx"
"B6cDr3gs0jTknCMpkSqQr0SoD6JLMteasq2pKi2tHin6LC8wkTGKKp4tSqpLrNYrOC1arO0tnahyL3YrL6iWp8Ut4SdIqr8ZZZmuJkkyVapLq0ElIy9dqG+s"
"USsPod2Yy6PFJp+wqKCTLigoai5tJ6IsialFqFgrYygZqRotEynusKIq7TJIrT4xYiksMLgq5bIaLdqzSi+rrmuOyaypriGzey0THomsZK1Zk2YZI5ihNDMw"
"zawjrrmw1CUhJq4nc57RrOMp7ShWMGKnGi59JUgp3KT2LJWthSwGrt+h4ituK/8rajJCLa2pIykBnekqHgCzICosIKwtMEWsLarfKBYr1SWxLH0uWB0srJis"
"YytrlZIoYDDjKSitnTG/o0atqxbQLEilNCIyrg8wmCZcKtGpXjBSqzUiIIZ7KbSkbKnjIoSoRKQaqYurPia9MDMr9zAqK+esdi1NHXAk6yxrIWCy8acVJW8q"
"JCzkIk0ieyg7oVCndqXOLP0g9CdkqBmnHDE5qlGkbyRtrTGmqKlBKY2r9CdILsgpna2wLtimurFVsaSunimWIi6kP6aRoF+vaa1GpP2oIi2asXUs9izEL/Ig"
"I5uSKuIpcSMzqQYbXClNLNwuiCzRoAIl8SsMqPorjLCFqEatri5mJkiwSq04rJmhEq//MDws5i2uJwQmoCCQrpsgkSxjoyatAytLKLyodi0OrNElGKfVJkmt"
"pi7dLigRiJABod+ms6z4pxcrBqogq7uggihDodYtaS8lK34i5i+kKJ0yZzElreqstqQGrL6wuqrQLqkkrqs+ozurayy0MDIqSCwPLNUv75i8MB2u1TIxLNqs"
"AyxeLRwjQqlSMuerRqtepyQrN60EHsMoHiUGKk6uVCaHryk2TqRZKzCiKyyrLGmmny1QpWag844Opk21NjGrLXQseSrYqsWrXyFfsU0mTysCpLGrZy0ko34y"
"QxXCqPMsn6p+LccnO6Veo+GmNqknLs0tJSu3qJWosykUMDKs1i1Zpo8sfKsiLK0ulKeTIV4tQayarCuwuKJNL1Ks+Kv6LXuowKmaqkOlSKmiLaAoZxyyKgQu"
"dDEtKiGs3y4QqMGuJKkMMKQosyoOL96hayNFKaQrNi/cqRAg+yD5B7MnSKtEqccbHCtcniSw2iNpp8mqBy2EK+ujeC0ELbAIUq1KLIEnsyzeKR4uR6tNKcIr"
"xC3uH1SwBKkjMAKwSSWtoAOsuCk/qdmohyaTsKAykTT9n6OpdK0SsN+tTiUvL4s0sq4aqoww1aw/shWlYaqBBdSryKg+qakfjCZoJw0q3KGLrPGpBCsoI2es"
"YC4FmHGwracHqX0rlK7lqH0tyqs7qjaYziuzqaqoCqwgIWMrAbCOKhMsVJ2jJpewwSw3MSUtXSWGpZAsG7CzKbeU8qiLHsEQjSWIGaQlWy/OKoitQayBrOuj"
"Ii1jqdmrRCk/lbmlIjLqKrMhQKg/rIGiJx7WrSknqTBurYEtbqfEL+WntzK2L5opFKUSqjgu1ig3MZoqCifDKA+sVipbqv4yyzE8HTEkFKNwrTqkRSS8LGsq"
"3ahxKdKiFaUhsP+thRqQMWgub6whsXorlpxdK34yfTHiq3ylHiv0NCstNqwzJFM0IilmqVKxpKbxKQ6v9h+ArUquw61rrOelXrDeJHcmXK2VJFKwJ7J/E9Es"
"uzAWLF0xnaaXsnStZCr2LLAqVinhrQGv+AmxsJIpIy8BseItUCrgrGUuHy/wszuz36kLsKIl+aEqKIkigLAMppkx75yxKVIvdJujrEOscqlhKRIwdC00syW0"
"z7GoMYKoDSaeLrWpBiqbqu2lTR4HM6gjRCrzKQ8s7atvIcSuK7B8LeS0Rp0EnzsmoLJaKtQxI5NbKb+pSZyJtYyx5C+QLGAtkTEWrH2smydMmAKt1q+8rAmh"
"ErKhNWmvRqpdMc6lGK8oDgIzvbCPJRmqbavPqk8uqqVFn+wpBakRhqYl6izGqkMkOaQ2LD4sO7AWppEomyzkqD2qeiQELkOghSIoLiGsFq/Drymn/y5Ppkyo"
"d6MKrGKpVC6DqxIqxS9SKHIlX7IHKbkpRSu1IbUrmqo4KPAtMiV2ntCfUK4+pCayRSn2LDidojBCqzkscignnlCocaSTJogqJS3LLgQdVK2WIX4wyLAPLnKp"
"CbBjLMYs9yz/rS4dtSVnLYmxNKUEKwqs15ZKMBIuV6guqrEeb6+5LfYogDAxsBWpUbAynE2lQjK0JAOs3LJDKqssQrSDJa8yuKYPKeOwpaZDrCcrIC0mp04k"
"c6ZgtuetSi04EnQwcR4CrKIscaLmqlOqjB28KjapTi1pLQ0tuyVuLYqvmyfEKBCptKsqLpmp0aSYMrak9ijiLGIn3bDrsEowfa1DHi8p/S2FLLCtETL6qLUp"
"X6R1rCQvwKpYqCusCKncJxsodKaZKJQwiyrPqrutmij/pDQhd55BrEOW96iRKy6xDi0ALgGd+5IsGp0wga14JfutapsaLIkqtqg9HPgmDDKirA+rsSFpLvWs"
"XClEI4CtQC4xqKOpTqkeMVwsAq1JKHYusKcSqGkuZR5dqEWlYSvZKUQseSQ7IqekGqruLAWs0KgksRIhypkML2klNZefr7OngKgSqKApoDKIHHAk2S9zMOct"
"ABX2Iqir36q7r7+sWi2PKxctoqqlKx2pSSKmoG6dvK4NJdwpEaxJquot7CzYoI6t6KhUJsWuJjAwMCqlzKv6JSAlQSwvrlIlfi3tJZ0oU5cirBgt9TEmLc4t"
"SiU6MQ4sYi4GNBUlAKZsKW8nUyR3KqMsAimoLLsIQzBHsNYx1axvrAGdKyvzKyWtAC6UJoko8Sn3k1QsRTAeJdgyZCmhreSkUioBo5Km3TBJrfutOyFYqFOs"
"lBqsq3UnJizjLKgl4a0arAkjQCgXoKggqKkZJUIqLyuEGoIsGpAusH6uz7GusRIliC1hoW0hwa22LNIk96bGLNAy4Sv1LaMx+SWqKl+lb6lwn8SuxyviKOQu"
"Lis9KNkv1Ky0rWEwZKp8JSGW+imorD8oV6AsK/spICyPrYSqKSn/LCAzO69AqNksVyz9GPEiD50LrEks3qAFq6CrjrK4EMkqySyXMAOosqs1MEYpU63iK6Yp"
"kaaJmvGr7KpBpV8rDSPzIxKtxCYPoNOlO54+KXeuliYqMF+w+S44rPourKxmL9kd4qjmLMEVwirXHO6uPxl+rmOo7qXVqukuAahyKHOl5iVxrEYlBSj5KqYk"
"3yoKMPSqrjLgrLwn05L3pCenzK9KoCqhD6ixJY2vrZ9Grxsway/hqIGs1Z6RsGqnurA8NMQl46wJp9icYabNKhQOgK/BqlAt5zBernmtTy3+sUAmraJ1pfol"
"Eqk4qiGxdqdqqEkSQqmorvIsFLCbJvWo26cXMPmnWiWsKAWsFqpqJTsqbK7pq8WwxSoALnslJTCJIIybybE8rxgk2ipDL76pvCxasEuawKgVp4qnp61sK36m"
"CqrZqGOxECyRLjetCa8yq5erJ6s3owSjMaxsrausLrCsKFwyPKl/LQquKSyOrGmt6i0BJn2rTa3eKO+ssLGKLLKl1axnsdYs+yxjKyctF6HWpCqwBrGpLtkr"
"ayHvnmExzqzwq4CuUaeLMTMfdy6HsbQkyieoLRkmdCIwJQ6s67eWL0WhwxnBKVAtfzChrxssVCtTKA0sqqcnKSktritPKDwpXiaKLlEgWacmMEYhY7BpK44n"
"gq4Eq0sszBzlLJAnFSwkLGSqLSxxLhYouiReImyvIa2VJRyomyxYHhSem6khIYwoNiadpvGlsTDurHocMKh8nLoo2DAkJ0WkjizFqQIiKSzyJ54pWiXIqmgw"
"8ZxDqdUhmKtOK8IoOCLjLyUl7i29rPsiMy2wp3kpZ7AUrbKn9CNmJo6gzyTQrRCpSS9lpkiqWiouLn+uMCwXptqmNy2SLhiqVKEIrSA0I6opJWgZghxLKpIs"
"06hDqAY01jMwrlImsiyMsLSvNiwFMYgzO7IxK6Cs5zQXJIIqV6gOqJEqYi5NKJorzajeLAAt4yyFq+stByPxESguGil6nXalBC4rog4sX5crLkSgwKj3MeUs"
"KCN2K/qshJo1qAQviI+QJ8wigSR7rHClVi2FJjqdMibxJeCp8iDtMNwtzq/aq9OgBaeLKR8qQSbcqB0pLiJGqMEukC0tr0sl3akOq1Gny6a9oISrVaWBqion"
"paUOLUkvIbEXq1arjqtHHTArPSu+rgOveqOwKTioKiUNLguunqpHJN+oVCvIrK0u6K38ocGfTyR/GWKo4Sz/qHYrcy+tn+4odisQLbov4yxJKW6ycag6LMAw"
"Q69lE7qtZ7FHtDUt4BuppzK1wC4ALF0tDqpFrIooW63XH1ioi5uTrnWkWKifLzgi+SkBKoyvYygbKt4vPC1gqvGVo68pKeGskaZAo00gCSzYrPQtkijpo/Yy"
"ECYoqBusJy6MrI8p+C3sKbGrEatmJackxRtNLDOoK6ajsIOrKpjHKsAxBSZpJ3ixQq0yrb+tLqz2phKrsalboIklzi4VqBAmjqvir5op1ZTAM8Eshyo7LKgc"
"IJxkrVst9SimKVstS7B5qQWiWzClGcwoIKxxp4YkgiP4MOmrex8oqQorPTDHq2mpW6z8qaWgdyzcHXAsdKzYsa+xKjFnLhuw1y9dKqArYCr0LeIkizT3Jd2p"
"4hxEMSAdJinNMSGsOJ5oImeWm7D8oDCpOyQ7oFwyNi3RpK4jtagBlwgofyAOnpQtgCxxqR6tkiONLMgmgyo9sLYskaOJLQsf2xhkqQek1JyVKncuSKbGsTEu"
"+6nEKGKUdKh9qUuvQ6lYMYMmiKcinjwx/SsxL/8tL6lNoJQqkC0BJDQsZa6yIGajsy38j5MseKxCMt4o265TGYWrTCkFJLYszye5qDedbi4HsTYkJSw7JC2k"
"KKQzJDerLiwCLSEsSiWSqSovC60XNAUrUS2iLW0s5Sw+KDUrSKF9KPgdSq9gKjuvhKz0rBquIDKfJx8oeLBKJAkpxTAeMTs0hqwKossGhjRiqM0ucSxWGsUr"
"4SRSJYIuTZ4Mo38gXK3xJMWi0SlFplohLqzzoS0uMhsdK2UnuiyvJZEkii3NK0csiS31KTyvCK6FKzkn9SnhJ6+pLa2YpSwn+pveGmeusqq/IyymeSUGrVEc"
"ITDUrN0gMyz+Lwuv9aEPMOkpv6Wfpvqv/K14q2IrMaRxLwyumSzdqlknZiBkK3kpQK0WrHmwsDCIMCggWKwyKLgjKJ5Yo9gvkaWTKyKofiaRrIwmKy8+Jrom"
"y6PaKLurGKHRKFCm1amsJXigGC6Pprwq6KhUoPssI66Rr0Kx4qtpLokynqx6p9au7C9/pHQw/jNoNE+wVgy4oB0yVCs9HOaoMCEeHquiYS6DKHAonShFLmGY"
"+jF2KqyqOx0XnLeuVSgbLR0xALH7pKcsd6s7rJSsii2PKjQunirwITIqcqmVKcundar1LBElbh9eLS0pP6weKU4qr50hqpcuyTGdqWwZ66dGrWusZSz4Loot"
"WDA8IGApS6pPK74xDK2yJu8r8iCzLfcx0TRJMA0wN6tdLQ+qhazgLPMuCKxoLRKaq6ZIMNEyEq4fJE4deSghLZUuuDKXJBmuv6iyrNIrsDA0qp4tQKE7rQis"
"ly7jKQ4ufS4aH6Qr9Km+Ig6spzXVp8ktqrCwsCauejK1NRatV64No3OwK7WHMckp9KG1s2crr6xHqKqhqK2MLDQwSCvALAsgrarmLKomezEqsYYnciz3rwUi"
"JjCDpT2j1CbBKimkSKLNK1cwT6QeLNWhKqppqe8sDawAL8+owSeqJv6kkilZLJsmT6cJK8qkeZ/TLRIy2qqInhMtE6xqrXQoNqDJqemu8qyrrIcrNSrjKyao"
"BDJDrUIogqxCLQQrXbDUou8syqtDq62owq1Dq58wgCocqzysf6bcDfom4q2KLdwnPiJzFZEakLEGLFgqQauvq4qv+y/7rFywvKPvKUOpQCxyp9mm16nwpPGi"
"VyoUn6w0yjEaMsIobrLAJ9ArVzIMKt+yKrB2saK0WjMjrDOsXq1CrfqsBawWrCitCS6cLZ4w6qxCLrsnQ605JlMvBKTGqtssOq0oKdMpejFkJ5arNCoqLXIh"
"yyyNJlGpg6czLAAsk6VTLCetpC7LJp8pCSy8KLcmNykcKlymlSx8J6mrey1eJyYoWKXPKmEgVKzvrC4v7KxpKpAg7iN5LYittilhrcExi6jaIQOlZawZMqqw"
"b6hHog0uECpTMNYuOyZysIko/aRgrAErQKULrm+r+6rsq0Wn5bDxIimxuaj+KMMotqcQMFYv+K2OpfArvamnLNKlqS2XqTkr4aSIlzGpux6GKLUubC5Or0ez"
"QimgL6Yxi6zTp7CsCrFQtZUs4qHQKLq1IJwRrvihzKSSKeIsH6pLpVChBCToMckicaKNKfUk4BhnK3wII6ynG64wsyshrdkoEjAEplAsWzE4La2pBC2DLjIq"
"a6YpEVsfpKzKrMcr5io/KUCdDJr4op0tTyr0JCQt6y6qlruscy2zL3wqmi2ZIZgxrx/mHfYl2ywuJ5El/7C7qEQsIzHIKu2o46pkMhmsHy3LMDyiOyHrqrEs"
"HqI6LConX5MuLPwhGC/HLOasODHfJV4rMC76o9YvK5+qLCMocyi7Mjyo8K47qnMpECdxpGufB6DkKvKoGi1FrhqnuyBIqBau4SyNMW2gFC7/KfGvbqcmLkou"
"1DJoKHyq8qwEMBekoh9VoActQLChISwqaqzPql0toC/BJP+mnCdcL1slWC5PnAMiMimhLKir86q4H3gpbapapS4uRiznrPalD6SOpoUm8jBfpU6akCXvpDGt"
"kqpWHqSsfh4TpZEo7KZGqCMw3KVIsG8oZjA+LYuubCnLriEoKZ49LjuqpaHwHmIy46lRqugtLBAXI3yqFDGDLcIiEi9WKPUs5SvmoKktRqwaMXer1SuurBIs"
"piCBJMswRRilKquovi6DK68N1jC5q2YwcaN8LHkrSSnFLkgtwq8FrDst+acWsAwmJrMJq5enVRw3szQpzS7usQ8nUSwIp9WwOS1qKVk1aqauG1gpxSqPLXMw"
"wquBsUAoECReLAsu9aF+ox0tbS4oLBcTH6j+rJSrCZ5MJDCq/SjjpvAZkyyzrVIqaadkMOwpoiU3q9Os6ieALlSpDTCrLBAlHqw0KfWnYSzoqDYnrqmvqhoo"
"kiFvK0orRhgoKOeuQxq1q+glG6s2IjWtBqtSJJ6wWh6xJu+o3ClQpkwvmiiCL/GpY6qyqjKw26qtromgsrGMGJOrl54+rQun0SJfq/ciZCR7rJ8n0yyHMCGu"
"ky2xKGOspiyeHZktDJ+nqT0syKYsKT+x0qyaryYvZp+tLWKyD6sWsMWzJq8nsUUs2zHdLXqmtDEFLuweuyNiM0gw+ycsLMsuDDBEJvKoMyjJK6olGqbnq/kq"
"kB3WqN4k7SKQJlKdIqabKMuiNqp7KrAt8adiKNYsqyQorUWtGzB6rAKvQq0fqWmYUaMKLqQshqj+oSsDkSYHKfGaDa+Mq1KnTbA0qa4sGDFKqmmq+Sghq/0q"
"8CRgMAcnA60rHMusXC1+mAYseSMRmhWt2iPlpx8nfi8tnq8vkKPor0anWC4tMWok56gorCOgXq1WI3svoCKgrIMtBK3kJoMhsaReJfMo+Kbar2wpTC1IKWKl"
"xyx8FdCuTSgALHyvGp8GHmYqWi8MKc+oXzQNL2wru64XtM4cQTPcMparIK7ELByUrLMtMGms4Sqbsi0iwCglJg6uoqjZpk+t5qsorMMkSrCTp14giav/qqaj"
"GaaNLD0fbS89rlyjky0SrYOvVylBJDYuIa1sLsEpTCwQKxEpxjAYtFcxEy1SqC+pCiaLMOipyp+qq2SnYqTAL6Ov2bJcKUuoja6CoPksFjD7oIMssCtErs8h"
"USrFLo2u6iHiLM2wgiKjKJYnNS3AK3mjk6abrW8qATHHJZOchy2YoEAwvCngrYOrYivUG8Skr6ipJXew7qzpKbEnGyuUrskp7CmZqiUk66F4rDym6KxmJ5mv"
"vScxME0tACcFMNyeWyyZLIWwl69wI7uqpi2KLhusRKdmpEmwdbF9pagy6LWsrF6hiSsrroysrCAbKJUgbCfxEBkthh0JMYEhlp/Bpk+kniipJZ+sfKyFLS0v"
"JawSJAytdjAhrn+bAjIoKGop4h3OLXYsMqguLeahQiKxoJ6nnqzepuAwjCw4LA0rSi7eqUEwVjCeqf4QqyM2LIgmBC5wL5ctpS09KsutwqIpMGoguCzHKgGm"
"wi82K5wwOipFLXSlFCeYESCm6DA5qz8ma6nnK4csHqkyMFYhD60oLKAssiXAqJEvRyYXLdunVyyoL06qdy8rKqCt/6gtrlarpKjjKwwtC6muIw+qmB0nr52w"
"O6VEImEzAifNrpWqYS5jLPgvaDKLNHcqOCvbpyQ1/6onpPCrCKpNJPasw6zcrneofitWIJInGTCPMKCpraOSMbKccic3IXE0KqOIphkkVKzkK9+u6DEkJQwS"
"f6c6KC0lpyJIMBcoji2zIQ6wZCgjhrkcbq0HMekpjCb3oTqgnzImshIxOihWrygrUKq6LiinYas1rZqvpK3uJlMpRKwMLPiX7KtEL6ChlDFUsdGhTJq0r2Au"
"iy3iMOsqH66hqHKwza4hLDIt8atnJ5EoS6ZDKQ+s0CyiIKOjf6k5puml0BLHH04u9K9ioxKsZyazIpqsM6qVKKwMFyizq9opQik4LQkt1aeGsw2ocDDnLUcs"
"87DfrJaoTbQHMRWsFynxsoGjB7EErBsqtqu7rKgoDaAOjmmeySVMIDoz5S2CrpMszSzfrWiMOq7krbIoH6COrRauKyniFluaoSCZqdwoHq7YLzqsTCcUrigl"
"FSNFJVqi9iiHLlwzMiioJr6nRZv4rJErBSn6qXyYKyHblFIuxyhAMqAvtiyNKdolxSdmGw8tAKpWLKqpGK3QqvOpJ6fvHtKsWx3+qAaumq23JMCiYayuLpiq"
"gagLKvEnhqEvqp+nJhWzKQYx/a65Ms2q0C46IEIqFqkFLrw0rCx8obasgi5oLbKs9a3NK9KlUpRmLv8oZ6+krw2xsTPTK0Gsqqymrm+mTir9H7IxVDDmLHUt"
"Qi6arNqhnR3QsECy2qzGKzasUKuGomOxnKxrKGAdYyrUF+0rgbB1ru4ibyyVJNGkMqYMrPmnqiJYqSIxFy2CqxeuCK3oJUsq86+cKcAsj6qCL7qk16QPnx4s"
"Pyw4KrkqaSUOL+Kr+CnNowywuiVDraOxIJzwLSypqa3vn4ovCSQCLTGm8SrCqM2syC2bq2OdRK7KJDgoo6riFMSdWi+krtWoMKYTKRgoMazDMMIoCKfXKcof"
"by3ArYUsh6hfJz6kuBxqKAalRS6LrUSr7S7lqq4cDKxysGWYgKhUl3YtuClErE+rXC4zrSgxnTP9q2wtHbJjs2idG6+EMgMvxiDFpbgxIK3asC+nBKIyLDUs"
"CC2srM+km6MgtDcs6rJ9JiceeK2wqhCuEa1zKkEonDLtriGnFyWbrAioRaj+MIazdygQrzour634Lj2tViwUKpGk766Cjuaxdasfo1ygIK8GKZYwhjAiqTyu"
"F60VLEIt0q4VJVCydLB8pD+rX6gKs0iw3rCmsRudFq2zqg2m9q7wL2GsiZ8vMCAsI63pLaqwua3xGcgniCLILA8YTKhnLu+qLrEfMfGuoS3/rkWyWZsQNK6y"
"VSZuI1kjqbT3HjmpDKxYHbKoqC3Lrwos46y/LIevaq8bMFs1dzGKKZOuJqnhkUoZjKI/rqOm96ypspiwMLGYKNQp/CM0L+qpvi1dn4alKK5xqmWrp7FAL3mj"
"QCOZqAGkpK0lGSEqGirapDis0qSbo9UvBatyr82rSqijpFipbTDeLhItfR+FpWQxBKd3MK4rIKxVLNqr7S2eraAlgC73K6ymZyHYKVitsjLznieqvLS0sP8l"
"CaAJrjwwX6/hsFInU64apLWsoSorojYxjy4Cqqctoi9erLutriHSrLAsKCmRI5ylKi+FMUe0FS2zrPErvKdAK8ArWq1aHvsrACU1I4ohby3pqAqyfqchMs6y"
"ZiWfMRCjIJkipR8tgicUIT2tGTO8MH8rdrbWrEshca1psCMwPJTBoOmszi73KU0qAqtNETQj+6QnnHInh6QeqXko1aqmKbKtSCoXpA6mjStCJQGtBiwpJwku"
"46q3JiItSKsFpbgw0y4mNJyu/qiGq1UsDS40J7gk0qysK0mtIaUGqIUlMzC1rDUkQi2Kj90m5CROpz4pt5+ArAGpHKGEKGssrapzJcCo97FMrzghSZ4HJomu"
"mx9zsFqbJS7gLi6nuizDJ4KtRhekK9+rVrAdrRUo86wKrUUrvyY5KbclVK1Zrtin8ip+st4wMKipI7usHpoHKzmuqyouqoAoXKzRrWMvw7AQqpOlT6azqbCo"
"b7P8paCrqDAOLIYrdChArJywGLICrL0uIC6KOIWqdaivqY6pLSxqrCWs07B7KqIhCagjrgSrx6zyrAocQ50crp0wjSrDLGKsAqhSAxUuva4vqU0s0q9NJ/cZ"
"RCtnoQojd6JIqaWoTi0zLYmpw6oDLDSwnrBnqVquKxnsKz6sdqk+ozwtvjK2Iq4mxSlFqm8oMy8SFpipOqWELc2sQacbKkyuICh3LBWp27CLoKwhvpfELXau"
"Ya1ysbQiaSnKHHyoqazXqPKwwyo7LE+tGZE+riykw63FK+kpA6cmKkGkk6h1rakiFSyHq3gtDZhsp0esY6meLwgXfLAYqZurGy3SLVSwA60dKqowsaliNGIx"
"GSokIz2v3aArJDkxGTlYoEmwH6yMsOWsOivVJmSqd6o6JNkqNa0EK14dQ6QsKUst/rBJLQcsSqQ9rL8bnTH/Jm4wpS9pohWuxivMLBivMCmspEGtJZ9uKu8d"
"Na8vrCUo5qyorPMteqWurIwdASxbKbYqF6yBMIuwNC0WMJ6sISmfoQswiKksGF+tWi3XJe4lpayAoBcx1Kkgp3yt/C1hrVKoBjBrLNgr2SW3qAWonq27MBst"
"JSy5LPOouyHdDX0waqumqcolJSzAMOWmAzJ0Kx6slC33JBChR683MeIoqqZqIzGtIycQLc0vlC7NKpcsVKEVrT+qvbDBsJWliDMhHyesWLGXpICsPizaNCg0"
"oayYHIUqJDZaLIsui7CwotIk0qgDLiSvpCWEJl8xvCzpJ94mCKMHKlEpPS69L6qlP6SNLksoeqjdH64kPK0HKCaqGKnfEd0tdSADMdAjqrB8rLWp+KvpKAqp"
"Yx7GJdSsmKMqM32vCq07JVSmG6wDp5CtYy2brlqrAaxvLkAsfTBhH6wlsK7NKCsklZm2qI4x6q0Xr8ys5i2QLCWqYC7prRAoV6qtqj8s5yjoF9yuJTE+qhcs"
"ja1UpLMySKDurmquuaDHq3Iu7yuoqSKyN6Qnm1irb6IZJMcr6qd+MWOlK6xJpUEu8inTrkaxlzM8sUAydy1XpfYoaCN4svIfhq4dNGcxJiuYH0wraq16sNqr"
"zKm8rAkxFB0MJY4sBqgdKB4ozzFBMPstxKhBL/Csh6JSrlgwsaUZsf0sAayyKMYo2y8+LDSyU6voqkEgTCOnoU4shLB/rdOogKidKmUm6Ko9qz0snasQoLMt"
"Li/BrsYtyKrOJnAlqaEqKcgrRKsnrFarLyteIEs0ErARpjkoPCx1qgWQlC+7qAgrBqvsqlQqMi5qLLQs6rDMIFKq7yurqHcoyKu2sM6qZyUuIKmlVy/KrXyq"
"KK2TqHcn86ZiM6as+CiMqgWjYx57LNCooKzOnKinWSSnpjOkBjHCLc0sYqv+sh+pcTL3NBIqPJxqnbat27QVLCWp7CA6tBMrPpxEKN2sACYjHiqsJpDUpKcp"
"J6XZMJcpwi/tqyAtGiz4KF0tBai7GC8xa6lEI8SxiTDaoGKs67DZp6urx62rox6u7alLLzgmVypjrVIoGqXCJEOnnC9srQ2wWqPOn4okual0qKYqka3HKQkt"
"JaVqKUspT61KpBMspR5mKvEp7xlfJ/0oFJFSJDMkWST/IlQr+6cWLZgkFyixLMArfiXUmNYY6ajwFwCrgS0mJyisvqjxK8YrBDBdH+GsES29qR+sDS6IJoEk"
"IJ4aLs+nDa+/rJOsnamHLY0uNq4HIJYtIbQHtkghCDBlr4+m9ximsDSpUyN+MiYxEyyfrJEp4BwzqhozzTCwnEufb66SJpeviSsXpQMjD6t2LjOxKi7QpFiX"
"tKqvLpOWrbGeJuIpyiwhpcMZhqc6II0tQyccp3qnvaU9KsKrOrAkMEWtAyfvrOIwRqV7L60vZas8LNEjhqD/rB2xxS2YmUAn3Si3MPoiRS3WLLEk5Kz8mAsi"
"aKouLpwgdi3yKiit5y8XLTkvmypdlumqaJbprnGxo6UJMt6o3qrFrhovvajwMXKrSiRCregtsCj3sDmufzCQqpSfTKcZmxWrFDAHKGus5a2XqiMpH6chodck"
"GyiSrIEtthJMrh6vJ6kfLlYz3ayGsDivry3BJ6YvfjG4MnqtiS/gLoo1MarSKnEjfRMYJrgsd6ABLCur2SiMr6MnGCx7qGupP6edK6mrz6DaJq8tKi4EqvAd"
"Byrxp4KoqTCcKXCoRqy4rJOoNCBLMr+vqiMhqGoovSKwqrsxIKyhJhQtbK1XJ/UuUS+VqM0wE56voWuo7SOrMUetp61cpCqsECBtKGOs3LCRrFmswq7eqIst"
"3ylzFYgtQK1YrVQsgTB3IqOw4C8drcuq1CFir/imkaqxsIItwqrMpEIrba06plMxFayer7GkGC+JMk2zvyZIG6qvqCyKLIyj16lUq70qPSzTrHStaDE2MAsk"
"byeQstgsGTFDMh4kZqizJwGwNLS3rq2oy6yzrqcoh625rHkmRi8KMJ6lo6teKGEoiDBhH3+l96QEolgqty9TJSklMy6ErCGnnyVyqpkqa58kqU+nHC3zKeIs"
"0CLkp5ofpSzUnwkoQa2zKjWu8hpApx8nMSkkqF6u1ad4p6MvVK1Qp60lQCuxLL6lTSezqZkk2qrEKx+sZLBrJD6pTS+7pKMpdayRrA0XezJIIhGopzBFKWWm"
"HqhnsZOaKiN4MPinzao4JwgwLhnLLOug1KLfpwykbSfuIkkcEC0jp4uuPKwsM+akXatYI5usVa5mKYY0viCerpGoWrEAL3+pc7BAtHaymywpJIssV5mesCCq"
"LKpro8YwtCr9nIGewKmYLSYps6GXMSOnKyYfMVauoqwsphEuJyVOq4EmKjCXqH6sECeZLFei35c5LfAsdCtOMHQunSXgLuutqirvrO8n2Kg3KqKszSboKH2p"
"dCrHnysudyiPq8qiCS2aLggqBywwMDiiEihNrNwjuCf8JJaoDqvEIzAsPi7frdoliyvvKsStpaKGn/UncCeAqPsqcysrK2Ks2KbZLBCqViS2qowoKasaLCGq"
"KCkrrZUUiaz/LV+p4Sk3pe6staY8K0WngqgUKMip5CmhrdKjhSJxsMsvViN+NJusyK8nLP0xW6xQqHMpS7KcKe8wgK1hoMswp7QfsQMtLqsaNjCyvCtmoDAg"
"J6VeMYWvbiv8kKMwQi8FsKgijSxeLdYkLzRCMBOuxarUKO2j6i1qKYUvYq/npG2tdq4FJYslUqsRKz2tm6gcIB8qAypWLdiigKWHJlWtrSfXH1Cr0Sq7LN6o"
"zawPKj4woTLFrxIwXi1TrBWrHqjEL5WkValMKtYmb6LNqhwyCC6FqH4rgauiLZ8tNTLyLIgt/CrzLF+ts6U8LdQujK4pK9KqRa7LLSAzH6zAqfGsdibnKoYr"
"OTEnGGqvraFaiiqaijFrJ14qaSZmJ1igCqGoKUYtGyyGJdcr7yopL9soETXcLP+pDq9irGuryDEhNZ4sPSWYHA6yJLYYL2Ce+iGAsEGtNiFbKwkkuZ5qqrcZ"
"eiP0m+6kzKn2qCyhDS9Jq9ssezBdLZUmfCpjq3msPCofqdkmTSd4pYkooS1crnkrvagWqMerbCv6pCGdeiyIIjAs7KwgrF4umqVTqp0tqiclqY2ukSiTKvsb"
"oKjvqXkxzS76Li2wOKt9qXqsOizVppglSKaZLcQqLSj8LhqpsS1KJ0opaqmTpZMoxqRbLw2oSZ1cHZuapCt8MKGtEyW/LAYu8KxpLfIt4a8UpQAlzS0Mqhst"
"dDKupNKYzaS1qBOquqa7K5QdAitgqqQso55NJaEpQiwUKQ6pgbIRJMsuAiR9rK8uiyk2K0qygSZ7rGIr5LAiMFCnxKZXp7QqpTETLPeiQyvbJ2QkD6RDMHou"
"zi+/Ka0kXquZqKYuASGnqQSoUKwMK5eoM65gMBStbCVDLGOuSKwfmv0jebCfB6WpxKQtKVSuoKQVryQmTSqmJGQtXKEiMz0qBai0rNAjRypQpw8taCRJrsmo"
"lqeKFMwhuS7sKfCn56t3q0yGECqlLZ8hrqrEpngdEKviHxeoMjBuq6ct+ayHrPqkBCuhrC6vZS3IKXKnxi8TI96rZ6hXJqQoiqw5qocs2SZLsDAt76VTKo8u"
"saqWI2mfui1lqfEuVayvMIMwDyk7pdWyeiPSMDc1Dyospf8nFrDptZMx7q4lrPexAzBWp+yrdqTTLfYv7RkdLA6rCxaarlGidCpXLUUtNyvAp1QWeJxgpWut"
"i6UHHBAsmywyIB+qzqSepVWgBhllpYsqI5nUnRWuD67RlCyteSHqrHqcWiZQKsMpzy5PJbCgD6RmJjUqmqnDrWGsdqbEpWSl1CanLIKozSMkKuisxKrPIAIh"
"laY2qyotSyZAp7AowSp9rb2ryB+SJPas8CZXLJCcLCEaKrct7SWSktIsTaXrK+Yscyw3sD4oMikWrfwlfaxpJ5mn7ibQnEwsbLKaMbIkqLN0qNMtB6xIL6Sr"
"5pwvrgW1vC12N3O0qa2bKcyurSpKrHki/DbfuSSsdqbErVurqTNlKyytAyVwH/8soa5urCYoMLCrqXErca05MlMsnixarm6r3BdSM/klKa87oa2cK6v0rgEm"
"pC/Ho9IncrASMW0tRSl3LXUufqqhrckd2DKUKtAnrZt/qoCvO6RCMNIkj6VgsN2jSqZyr0MttCtcosAsHaYIpDQm86uOqLgg0az/qUisTZ83LFsuhyqQrwCs"
"Vyp/Kecw0iyxKHqtSCTIHamqEC7apeuv1Cq+qL+spTBnJZmuE6zvLYssRSiEIhssySx7MSOsMavJMMQpFK3SrNquzaroL0gqMrCvpvyu0B3IKNSxHLTOsZQw"
"TSr+pWqizy9RL7KyCiXGqbQg9bKPJfqpeaxqsMOpsZ2jrVQlKh6EqIas8yypqbUr/iZULPgUi6yLqMun6y01rsspliuTqh8qICuxKe+p9CW4I5epyaoTK26m"
"t6eQLZStGa4kLLikMKxOrq8itqwQLmsdkSIcpDwa8igKqhCs+SLRJ8OpvSZWnvEkWiG0HQEr4a7ELBUmwSaMrV4Vmyt5KOmqAZawpgopnikvJ3ceXCjvLm2p"
"xy5KKFskyie+LNUvKC1EMfKsWABzrv4lgyvSJmIs5SjVqhesZKyVp8epBB4JIPmu5idILCkqbqvLoRYkMjB4NW+trrcHMMozKrLnryQszqQnJFgrWy/YMQmj"
"5iQmJzg0faY9MRqkFy1+phQn+KxoJzSgMStrMpCryaTPnQ4qP6UPrD8uq5PlH4kfbKhQnM8moDEMoI4uuaVvqbAhTaykKzwuMiqYJSiw4qDRK34vgCfwJaio"
"06qSqGUtcC8BmSEehCyfp7KbTKM0KBipki0krRGt56KEqm2qtaMxJ+Kc6KLgqV0sZKKnqmCmQiSQpTio/6rPqdyr0ieznzypIankoWyYcCW2pMcj+ypvqRWX"
"rh7QJkil5AuSqeShZiTjKa6giyCgIWOreK0Cpj+ZiynFrAamWTR+paeieygBtEa3jTBLstW2kq7gMnO13itvq4UxyzJDqLkpUDfFJu0rYSXNpQ0nLigyJ3Er"
"m57HrZ6sZ6gvrH8Zr6seKa8rKSauq1wt3aQvrI6pWClurGUo/yvMqkYomKetJp+q/CjyLWwqKKu2qD0d7qvVrIMpUCYaKdQnViZDHZkpNi3CJCIsEyZlLkOn"
"NCWlLbYiGCt8rSGsa7BOHdAsN538q5Oi66E/pdwybS1hnwytLaitLTYw4C9GrdmcDCrJKzOsAa7cMDWqYy0OrlIpn6pbKo8xES4HKs2scgoaL5gs6jDZLooq"
"1BN+JGMrPa7CMq8ojy2TLr0kKychnvksPy50LAaj8CyzLCOs+bBNKp2rsTLuqfUlCa9UnkmpeDEINX00TahXKl4h/jJzKCcrmqn6LTgeMjDxLjCw5KzIqasx"
"iiUPNByo4yySqxUhlDC7KUep8Sx1rPguTa3eLUMsWCU3L4moPZ3JrPYwMidsG4qtJrFcL6qlzCxurOYvcy6MrJwuBq0mLLSvxqrdL4mqQi6MKGaqTq9uIOar"
"2rB6MJGtziMbJ5Kwcqk1r3cp/il0Lq+qpJ9bL0YlCys7q1oifa0Sm3UdASxslvaqQjHrpn6rNSPGr6sfKy1ZMC2hAR24rbSmgh4Hnl8VDyvRqHQn7DKdq7ys"
"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"
"RzGBrTSoKq4TNMk1piK9sKwl1q5IuBAyGKvjHxOui6musNUrai5DJU8qATJorbIqiywzK/MtcSzrI9klL6zbLZkvHC/pJgwwYykOKFGtNiFVJ4wkySogJ32t"
"OijHGZSwFiiWKBMtqSyMra8uN5xKJTKqcqZan3usOC01JoqrHy6pp86o86drLAAozKxsKcIgdSyQJBcvZisxqYCtRjPQrUUriC/YI+shhS1aMgch2yvYMIaw"
"vR9CJjYv/6EWrA8ifqz1p9msOzVAJ86bji+wJBaqSaxyM28pxKosKIehFCyMpuIwWKkzp/ErPzCgpd+mhh0NLNek9yjlrgguiCONsWW0mK0zLj+uBy27qdUg"
"Faw8LDGoJC78KFctPqUaMCwttynjrNAhtp5RpbKfOak1KeOpq6klK3isfqSkKn8t2S7PpT2n6yv6MPyuHav6plioRCqmLE4uai9arGEtKyWjLCSilagZNIkY"
"AavJrMAnRSyNKYQyeS7lKzesky2jpL4yyiUPr30pram4LhglUTA7pGKmOimcq+ieeQdGq/8rya2XKmEmdh3ljfktrp9/I2IggaBoo+Cmaaz4qi4v0ysolOkt"
"uySHqVSssqz/oIqoK5SdJyUoQ648rT6kNpuzpIWkmzEErikvhCiRKuoth6mJpGkq0YxJMHIrKiy2LGAroC07qykswa8SrisxlSsAqqUpJ6hfpvaw6KnQpw0p"
"67HlqIkr6SHoqaYn/azOJY2isaouG5EvTq/eJOOuJKyvKhUmxyXxJbwqgzJ7LJekUq3WKKAoh6SyKsQtsKwBKvwrPalNJYYwx6psqskscCgBKgqubC5Upbgh"
"2yv0rFaoFq+vKCIj76l7qE8sSxw+JU6tP6mLLRek66nlKKwYnK1XpjMTrqfOLQgxlCwBHZ8rNShCrRYvqKhTppCwWqvzLfyfUyKqH30pdq0rMVga7amZKV4r"
"EK24J1UnQ57iIMKqrSSMLNyu5Sd5KnWrOiwspimlWSyVKqqVz6tjJq+zwa3toXqtWrKKrzcvWjGLpkKtLLM9nXOk+yJBMGgyLiU3pZcanCyMKK6vxawwMfut"
"yy95pJwbuKqPMTEzuaj3KOQrqCmHJeclQSpkrYWpiKqpKF6tUDG/LXGmu6larUAkQZfzLvWTICpIpwqnBC6qKKKt+alKGfktC67SKHcsYy9Vr3CjQCe8IBKq"
"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"
"KqyQHUixBixXrCIrdSlnLmoo96uYLycs1ySPrd8pparprqQoQicALFKtKSwSqMys1S1vn6QjA6cHqwsxNq7DLXorHRkrpcidnSavGRooZKkOKQglC6k4LGUs"
"FCa1Kmqp+KYZqMajsyhbKwgvH6WIItCrryocns8lPywtrQ8jPCt7Lk6qaiYWLHKsUiUeLMIpeSlTrCmuli3moo4Xny7OKOUp+C18LEqpwJyjpDivAjHkJCQq"
"t6cRLr0rD6WqMNIux6ksrQOXA60QI4ItuB/iJUMmoZ8vrPcnSaGhqp6r2y0DLc6ovbIlsEmw5S9wM48guiRZr+st7KhIIog08zQcq7YpOinLNXIqTKIeLIuv"
"iq7to3iu+q2tqcwnXpmWKxOw1RnCFCoNXacKrRCl6SeOsSguNCKRrJStuyRIKPmt+Se9K/GmeKhnMP8h1K/XrqQsByTGqh6xo6oDMaQnIyYJIuCtMqo+F0gt"
"1qddK+isD7BxJYyt6a6Yrc2tNSzXE68ndKRWLnerWijvKie0nq2+qNcmPbI1KtyorrH3LEKpWLBTsfYu3Cw7rL4c5y0CJF6vvSGKqx+w5iZ0rr0p6a0dpMqs"
"8hVxLmOt4KMcrMKuuyYIq/StJCmdLB+vJS3TKIOo8CkysdOp8aEfNLQjWTBLL+qneqqZM1qxFLCIpZYyMS2NqFcT/Cv3phOyLiySrXooOSzxME+pZB6fJ32m"
"D6i4MY4teaMmqgcpW6VpK9atkyuSrVeWs5Omq+cuCB2QLOOfQp7hLD+vZiysoUUr1bCgrGksHikbKe+qXaYKsE0upx8oK2gtKa9KnkWsNiw6mp8vTi+YrQ2o"
"FTGDqlQtuSF/G7Us7DAyMB6vxh0in5woth7EMWepMidILPcdPim1qYOoC6thMN4q36uPLnSu0C7JoJYnba3OLOisg6UYJkAsbKrzJiAs9quKMFkuT6lELfks"
"k7DgLMUkFqZTLI8rUKu6po8s/a6YKRkbIinioK6zy6naMGkxuy2OsGCpRrKMtNEigBRRo62xmq7cHp8pyyzsHs6iXKp3Kw8lHqx1rCGlYa5jq9wqiCsZK2cq"
"16qArXeqAKu6oHcsx6iiKiIsFaxlrFOpt6fPrH8qha3GKMCssyVNrLurxCREraAePK3TrqMpdCzdqrasaCk0KsIp2qkWrEIqSK4orOuoU6pBrBAqS6Z7rM0n"
"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"
"zC4BLla47ROELWQswilPrNMyByI6Lm4tkipYqGusvqlALRkegCyMrZCsLSaCrqQxOTHQNPuxDSn8MWQxOywcLQgv0TE7r4MnWSxbJhks0q0UMV+sjS+aM9Ik"
"N7KFqHUwKzF7LrAyJ7Juo+gkDDC6lEkw5K6oL1EvIbCmKwIuPyxzrx+wUzGqpMkpQywSMvstPjBFo84sDzOCLGasPy1mMp6tFah1KCIyZTO9MMMrgLR7ErYw"
"x5pHrB4qTTJWpAcxDqxvEFSirC+DJi+vozOBKIwvNS3gKQ8utC8irNuthzCWqPGo7Ko0MVqhCqi8MmulWTCVI7ymkzDRLw6026YQKg4zKiwcuFSwDi36KO8r"
"P62IMfqsKC9MJYMutamhIsWwZyp+MTculyJhrIQu4yHmMWgxDzVttFgyLzI5Jlk0eyY5KwYlSa2jK2UxGyJurlYtIzKir6suRDEjLOOt7S+WLj0ynZkeMqqu"
"KKlJpyAwh7DHMJWuARLMKxKZaKp0LuotRapHrRAtzizRrJMv7TFBKBQtR69xIXcxFC1LJSUy2jDQr06qxy5KLgYzhiwTKrqyASgtMH2kMKsfpwMpwSlIME+k"
"OS83sGox+6cPsNoyOCmlJVkuiyueKUoxIa7xqyEw0a7hqjOxXZIsL/AtTDERLS0vdCzbLIAhnjGksDqsgCnRLissE7gHsMstMC3bLr6yayUjsMIogy3rLTIv"
"1a5hry6X1C9EMKClmrBsMM2smDIWLGyqbLFeMaUyKzEVMxImnTDlMrGwRCrhLJ8o55a5qXkxC7AwM+YvuywOsIUxPTH/LGquEjIvpXorESKULRarA6Girt0v"
"bintrMGsUTHEF4yxpKz/LX4oKaieKcUulyCTLBywnhvgMiMuMBsOLJwyRiAir80vljF6LREyfy7mtJyj+zDQrMesnyPSLD6iPit4r0ijUqXlMPitByWQLIkl"
"MS0TLY0o5KLBMeyuX64uK9yuLrF5MguurqoUroe00afxsUiwrLAMpmYoUSyuLiItHq/spyQ1riiWpXKsN7BeI1SyGKiwsG6xbp1yK9oxozGiJCyaw6vXMLky"
"+KkjLJKtorBtsygtIapbskCwUa54Lmexxq9RMdKskbC2LxGWZzGisgwpZrRjsj6mmTCiLI6yI7Eqnk+1xC48qEgu3q5ZK/quQzDvr/yqtiyHKQUlOK4vMNku"
"pjFaJvCoQbDwsIotzqgOoWmZirMXsAgtZ7D3siYpEamCrz2wfbDwoNCy3TQcr0yxVi+gqzwoa7BcHM2qaDEErgYtz65pMW4ssbFNrt2gUK3pKUYuQbMyLQ0k"
"orBaKzkvtTGYppKm06RMsm6uZKUqqpec/q3gpEAuICuYqcuqPCh7NH0r6q6DrisppS9UsEArSrB4rB+woSXdLxwn3KV5sMys3iynMNmwizGIq2SuRbTFL3qv"
"yq/rr4OqaS5Jq/ytPB+CrXqwHzDopGkwpLFBLTWxwLRtK2kzmSbisV+0QyxosK8ygyIbLJwlmChPqJQxG7C+rj4q7y2gMNytMjFvK0cliLAEKMqqm7M9KCer"
"ViwkL2WwxbENL/2wJaUBMT4miSawsBOzHbCYrKgxNKRNslAuHi3/J4ywvCYTrwMw5aj8LVauSTGOLaCyabCRLtWsgy/vLsOtXSrFIO+un58qMI4r8qTdpRCt"
"2Cu5pY8roKNVq8knnaqwnG8i560vrU2rpqm4LC0lvK0OJV4g/K0iI0csxKYLp4Asq6oNqTkmuq1AIH6sN63Vp8qp7Sp0qE+gs6kQrVyi0yV1Hy0s7qTqpFwo"
"/h6rI3yqbizPrNSrL6leHo+mbBvjqMic5yujqH4lChhIrKQl3SUBKeshPSO3rQOoB6hVK0gmUqjVKC4irqiVKBMpIaB5Kb+tziySKvWt6qpGBCuoBSE8K0cs"
"Jp9wrX4qO6f3KSKp3yy2q6WtAqmHKWeneajjqi2tJKzkojipK6feqUsreB9WKFmtqalrrcisOK0uommtxJ4AI0+rgiiirFAvIajwJdAyfjEML00w162kL5gs"
"OrN8r0ukBi64Kuy1Iq/9HxSkIC25qvYvxrCPLQ4sCS2go+mw0LDZL/QupizEroOulB7LrZMwaCv/JWSwoB5qM54zai7KoCQvUzJ4rjcway1eMKCu7BAmMhGt"
"rTHJMq4oTbEBMoUxxil+sO80i7AJnpGjsyjZrN8lqSsAJ6ctiKrkrBgtTKFSr9ewq6gCLE6rGy8WK/ct5C6nsVitSzEkLPYumzBdMqep7iktKd4oETPPMA+g"
"K7IAKzcvU7OzK4As4CqdrtUtva6XI6inQiogJ7Kq+TGsJAww5S78rfEvKDENrcikyTDKKyysbDFbKNsjg62qsR2p8aWhrYaw+aq8LZkopCurKdqsZ6tVNSUe"
"BSobr+2ukyM5srYp8a86rMuo1io3MIIwXyVQqscjcCs0MzarljCQsZOvD7KvJxewm7GmsXGpcqIbrRGwBC0AsM+yPiQ7KCQuC7L2ng60mbRrrG0xTTAesq2y"
"DSxxtVsrrincMIsX7qRqpxEtGbBTn88pfR0eqGCobTEaKnYsiKhhrLSoNLG7LeWh3ye+LxKx66szk36yY7G1JtGlDagbmm6yFZ79qDs02Ks0tFoyzandLcCu"
"IRsWqakwAp/KqvwqVi7ILJWzMap/LQWwHiqMmbyzHjAto7mxCScXJSmxaqoxHDImUzH6MF8mUiuFHAIvyCXtsKGhQRMAJskrfrduo3AjGS++HmyvsiyosDIv"
"CC2kIPwwxLBTsSMwkC02L2CuSa4pJiip4DCmMKIw6LS7JHct+ywvL9ck4S0NMHqvJjByKvcpSK5srEMyWKndMUUysi4DpRcxeDCSLcWogTE6sEMuCy2lL2Ep"
"3aTMpgoxryojsAkYGqGqL4+xdqtQLHgvCq5In/AuqKY6LAKxSq9zMyQw5B2GLnAyHa72sCQvsDGtMBwxsCstsUmpKC0nscEouqyaL7UlZCzvrSgqVqxrKwSt"
"6a0ZMxwwHC5iLOKs6Ci6MxOxiLBHMwqns6xENNuvWCGSJ4exhSpBLXKpdqY4sOwwJTK5pXcrkLCGp04y4y5fJcel1qqeLoKssyZyrMgMVKeqLR4sEDGtLFit"
"YK4dL9MwzqmlMIWusa9wtNMuWayXsryoa6xJK9+ttyCjnroc4q8CrFipXSsCrxIkSbCQs3amkTDdp/ayibPcrm23QSn9LH4uyCiyrAqnuSokr3OsNC5ILAop"
"AC8jL6MlMC+IJdUoFi1VslUsDaWDLeQtV62zqQesG6B1sfUxkyzVK/CtMbReqP8Z3TWpFKWz0zDULIyhu7CokLSuzy2sq4+t9qznMYwoYLJdpbUZk6wrpdGb"
"tLC+FR0aMau0LM4sBDMMpW8YyCS8s1Oti6m/sDGuwasQLq4y3izjpgGwNyYwL6QqkrEosTyraDCirNSsJK9wrA2r5zDLMLguYp0UsI2ldTAnLROgPDDJrNux"
"EbWNpF+tRrNIrxmuhTBRp2ivyi4KsL+xES1BKOYuY6/wMNOzoLFNnmUzH6kGssm0aqh/sg0ykQ4dJ3GoXyMVsCcxI68rqNMxWijtK+KugjEZKSgtK663KV2s"
"gbKVL/WudTDEIN+wy7I5ppOvJq4zMTWkkqwUq3K0DKhYr30xSaqjsw4p8qlKLAKukSffrPWVPbAmom+sHy9uLgC0jKueBdKySCapKRi0wixxKoqn1y8fKxE1"
"Sh4nrraufLIQIOStcLFksBmxoyuoKiMwNS3nslImjjNSL98mU6UVsAoxv6qjJYewdK1ALQcqKTD7LvGwvLDSsbEsXzHUnHIrFbNKsp+1WS8HsICzELGbsLUr"
"JLGkrxEqpbGEsUUogampMFeykSqXrj+0T6u2NGkso7RgtfqglLV9L64qSTD+rHskd65vLrmw1bCDK7wiby0ZrTourS/nMIem/KrOrd+uRS8irOAw/DAktNMl"
"haVGsSivHSHXqE+maq7dskyoW6zGM7OeabNyMn8sLiL+qVwpd6xhKLGsEy6EpYQzPKhss3CwRamVqP8td6IksgYxVi97rHswYifFLh+mVCuclAohtKvRK1Wt"
"jSSfGwWts6wMKA6s/CpQKROl9q0brocW26wNMLMmS6mQrImsyC2nrD4ogaQ0I46u/abDLDikz6TUJpsc0KlirKMstS3mrWKZtyAPJxykiSwLHjGuyiuYrCyn"
"zio+MDgpyK88LE+lr6mfpFEodqICqbSrC6yOrY+uPyw4IkSuniv6nfuk2yyMrDKs6SjLq7wowq07qmOtRqc9IS4rmiAvpCeoKieHlY+ukyTKJaSoPytFrpKu"
"wa1yrvWvSzClJrAo7hzUpgSswK6dLRSwx6igqjKl+6u9pvKocC1nnMypR6oQlqOsGKsuqTOtii0EMBouv7JWo0Cfhqv6MCQsWi4uLzmoyin/LWCxb6/8Kf0s"
"Li41tmKtQamzLBIo3rCHnaGuQDAIMH4vQC0dsnWt9KMALtkuAa19rcEqlrFJMOIt5KQGsXYsPjLNMDAyCyxYMPkrP66eL3Qxbi8UqholYTH/sbwuLzJCLmWt"
"iS9HMTMvKa9dMQqs1ywXoGIwOqnFLQWp/yhnLp6waClDMLYteLL8r/8sBTFOsLAwKTP/JJYseK9DpwwywSYkKUAcCCy6riSwxizYKfMuJzJtLfOxKSgGMI6t"
"vhwFKZEwfyZQJ/WrRy3Spt8wRaVBsHQyVDDgK0MycSonLucxbLIFrWAudLFMsZM0wh2/rJEs6bGrrB6slrHNqnytujChNIUt8iQystIl2DRTK9utP6ycr6Um"
"D7IOK/uu2iRDr+agIDF2LYauGpxvKjMsnTM+KDIwFrGNqcazNzMKsZCxB7E7sRotaq6xsi0wlRSBspkqVy1iL4yyIzHprvut3aTJMKAuLbHutM+oj7MYMUEv"
"Ui3kobUoiK7wL2Akmq2tMmyo9azgK9sxCCwwqf2sXSgZr1Cy5iFGsR0wBCJjsdCoR6HTsMGvxy8+L7qsWqyJskqdeaweNISs5rIqKRAsDi1msAEwWqXHMOeu"
"5CPwsXwuuS1PrluteyANrT0ozDA9ssswJy0ssDcxuDAJMqqhRiF+niyu961ZnlOxqK67rWIj8DKWKbowMrDxJ18wAx2Hp6SpTa8/LI6kWqxcsNyd4q0NLdgt"
"zSsLqAKu4KujKq8xxihWLrytQrPptMkocLAqrWckErFFLmuln6xAL6evQLEFElGo2Sw3sM+i+LA9s0AfKDJDLVyx9bR9p360GTC1LsQuDhk1KlWxZSs5rjos"
"iTBrqxwt+qVSKKIiwDFMLM+qYqyurOcg0q7RKTMjPLKppuouWaaSs+Itxio6rbSq5LB7Jj6u/TK/rjKx2SwMImUtzqfgpeyxe6BxpSGlpa3CLQWsh7Akqf2m"
"lqVVo/otabOfLMQrc7IkAmmc/7FyFQaoxCzoM0wsUyyoMR8gWi6+LNWzmaNQMLYx3aAzt3CnQaN9m6Ywlq9BLW2w+St3JBmiMy7OrYmuVhn1LnipLa76pOkt"
"KrFLM5QwbJ7SsQQyvC4ZMtwxoqy4Ik0sUaMoMfkqyylrqpuuWjPLrLYzpDJ8qNKpMDBoLkMtWbDBM2ysKi8hLvwrtazdINav8C5+n3yw0yhwKYgsRau3mh2V"
"CCdzrfItCzMaJEMns6qfqQkv4y7Oo6ItSy1Hot6eKSyeMOIw3TG7qIKyValTMJCuT6nWKpEs4CWiKr2sMS6spx0xE68jIpMy3ixbJlgtBi4bGbIwcLGWrPQw"
"G6tRqRgxo7DZKZEuXbNfLOysz65nrk+j0yx9M3clnCn+rkud5jRaMFYfwaykKaeZsq2/qAuwZbC7KAMjTywgMJiouSDUq4osWjBeqgMuirBVrLuzjC0wsC6y"
"N6wCry8wK6q6seyXNrAzrDSmgiQhMICwh6AtrgK0QK+KMX4tD7JMqogiibTWLGgqOTDVFrSqtbFALr+w8YiLMksq7KplJxcrNjFXqzifqKhirSCy2C5koOUx"
"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"
"uSXIFdsvqp+WsKou8aHdMq4ochfoskAxRjC3MP0xxK6wMMcxIa9bLQgxriyUKUiscTMkrd8y6jNPLtGwXTLsLnkr0LB1NJOwyCSTpDcwa7CMpNOo0C+iL0+x"
"SqgHL7ktQLJtrd8v+DDisI0vDTN8LOAvIZwsr0YyEzEjLAovSSwXqYmwrSshMoEysy14L3e0uausMlOvmyJgqC0sNqaOKV6wlSvsrcctdKs+sFQteChsKLUs"
"fSlvKyEwMq8fsHQzHbGjsX0w5C/TKvytZq79qGasD6nTKDgscCjoHqOgJ69jLEojx6FkrNGo5qtNqmsqGK/GLIEeqSvelMSkkqrrKdKtWyqGK+ymj6RdpnEd"
"aKn2HO8kHC2VrFGutKo9LDuvWCxULVyqOyzKHyOo+CSeobwreygjJSAZ56H+rEKs7amtrXac3Ky8q9KthKA4peYnZCR2rLMqEK1xrUsl7q2arh8rt6jUKjGs"
"9K0kLJEpf6jXKVovqKyupKKpZxpgrawsR6y0oMEh0J/QKQGuUC0RJleolqszrAyjQKxUIRmbmSurHSqs9ql/LNKsbq2DK+seQaSJKvyr1a90LB2P4CD6rKoX"
"X6yLKr+uqq2OLJssa62XqvkqjqpOrqujpaj6pgys+6kjrrag+KpirWQqQKnVKJMsUiCKJleLB5rJpEunNhziLP+pMq15st+oRK5hJdmpJShjqMqgF6xOKcut"
"f63fqCgl0p7LpditCqxkrP6sriFLLA0awKn1pYOkSalKqA4p3q2jqHasw62PLAAmfSZepd0jwKjtrZKto6vtqXapLSt5rAIu46e0pu+f0iVCp+ecPC05KqIb"
"ZKt1LIesJiwHJUgdzSyjJq+mo6niGXGlH6jDK8Qsb6w9ISypJCu7LGmnQCaWIm2lsyDAqGOp9hpVKfomXywyogCtfSmVnCAr1RpzqiWqgSohLGWsnqO2KRij"
"1yWHmqcmX6wHrRClVSr1K86n/6izq2CnQiI6H6Ks1Cc6KsOnHa4TLO+srq3XpScosqzZrEUr+CgpnwopxqsyKomodKUkKjMgIauMI6aqTylgrD2p3KBkrbos"
"FCB6q/GrayyPKQAsnicxKX6nIaxypS2pNK0IpkEoBqtVppWhE6VBJ60YgKWlreOmdyklIuqpuSREqy8pSqjDrZOtly29rKYrGqdOLNgqXqy2KJCo2xuuqv8p"
"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"
"cCxsrhYwT6JeqogqvSkkL02w07AJou+vxy+WL/CyxyvbLYSwOjFJLKCxxKRdJi+oDDIfMDAwETIULqMwtTAWtKGwrSuTMSEwk7dArOss/CGXKWKxQCwmKLCk"
"Ai9fquIu3akaJ5svqS2CKcqu5a1YMOCdZzLOMHol+rLlMBsuQS+uMaSoACWHL1muYi4dMIsqcigeqT4vm6+3K+czb5QolLswtC7FMYOuyDNEsBwVXysLLFGo"
"mB0tqj8vDjBcsUms6S8mMJSyTaxUMFowe65komIyf6eAKbMlwKsQLpMv6SwkkQQxDbBjJQKkdDLJKoYxgZEUtEwt0y1vIlkdhKjzJNikmyKQrJuT0ausLeuo"
"/q6JMYcs7yIVMrCptCdsLkyvTK+5KuqwTJjKM3UeLrAprSGu97D4rB+yubHHspUimKJeLsAtfLAZqQkztC83MOanWKrCLTauBSxTsTireyo1Kl4uViwPsvOz"
"S7EUKfcyYihOMCKxN7BKtU0t2opqr/ioc6wDKs6xlCUaIqiyd7FVKkWY/C0csqij0quTtYGu2THsMHKx6bSvLMy0JSvjLfMuP6TpKPyodaS+rtSwPy1lJUYr"
"uLHbML+riyjlrPojhLIEsZGobaEErkgoq7AWJYkqFK/lsz0vRCP3qkytiLRer7GvlDUyJCG0ZTAwq8gsqq6yLKWthSyrqCCr8Sq0MbCoRbE1r5Epa65ELBCd"
"ea4+LaWlvq6HKYiorLKcKAAvsS4TNJEwIjCNMpMogjDYL/Swtq3oL7QxezAKtu6ooKwIMOQok7CmKH+tPjBLIasupiqssDqvk6akIEUceasOrVApPKGRMBYx"
"QawitKovji0xLm4w/yjcLywxg7GWMIQuNiw4KvSjYTRLr3QuWTNPm/OjeTCMMYMtOrCmMTqxui72IqKFVLBfLaamQjFHLrOwNqxSLkUwCa9gpaIqjiuWp/Ev"
"4i8eqricL63yrqExSyvNLnoxwDDArBSv3ql5KoQxySoDHqCypSaIKUmZEaMUqz4VZRsqMCmtnC8CrQMlUqjsqp0tIjAUK9wvASgzD58w5q+GsQwyC65QoQAl"
"gK0bpTAuli6eLakYLhkaKq0srqRpLX0um6rLou2ldikzrFonvy3gLRSkii0XILiqMC0tK+Evapr0K4gwgqhSLS8rECu0GuCsvK3aJe2qoSshqMesiS8fqnMr"
"3Cx8pSGm4CGApiGqXK3UGNqsraaBLJ0eSqwAKaktECj3jc8osTR1NFqzPrRoMz00ByCYMpuybrNztDIyPTSlLpKzijRrsg410TTVM1UtOrPCMsM0FbUsMuoz"
"d7PBKG+zwDJgNvCz8TIgtYu0e7Pvotkohi+wNO203TPDM2WzoDRcM4yzEbScJLA1GLR0NPOyNzRznXUzujNZNSo1izR1tC01GbTHLw==";
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
