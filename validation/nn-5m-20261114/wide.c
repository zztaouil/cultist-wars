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
"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";
static float nn_params[43393];
#define nn_weight0 (nn_params+0)
#define nn_bias0 (nn_params+24576)
#define nn_weight1 (nn_params+24768)
#define nn_bias1 (nn_params+43200)
#define nn_weight2 (nn_params+43296)
#define nn_bias2 (nn_params+43392)
static int nn_b64(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:63;}
static float nn_half(const unsigned char*p){unsigned h=p[0]|((unsigned)p[1]<<8),e=(h>>10)&31,m=h&1023;if(!e)return(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);float v;memcpy(&v,&u,4);return v;}
static void nn_init(void){static int ready;if(ready)return;ready=1;
static unsigned char raw[44260];unsigned acc=0;int bits=0,n=0;
for(int i=0;nn_blob[i]&&nn_blob[i]!='=';i++){acc=(acc<<6)|(unsigned)nn_b64(nn_blob[i]);bits+=6;if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}}
for(int h=0;h<192;h++){float z=nn_half(raw+24576+2*h);nn_bias0[h]=nn_half(raw+24960+2*h);for(int j=0;j<128;j++)nn_weight0[h*128+j]=(float)(int8_t)raw[0+h*128+j]*z;}
for(int h=0;h<96;h++){float z=nn_half(raw+43776+2*h);nn_bias1[h]=nn_half(raw+43968+2*h);for(int j=0;j<192;j++)nn_weight1[h*192+j]=(float)(int8_t)raw[25344+h*192+j]*z;}
for(int h=0;h<1;h++){float z=nn_half(raw+44256+2*h);nn_bias2[h]=nn_half(raw+44258+2*h);for(int j=0;j<96;j++)nn_weight2[h*96+j]=(float)(int8_t)raw[44160+h*96+j]*z;}
}
static float nn_score(const float*f){float h0[192],h1[96];
for(int h=0;h<192;h++){float v=nn_bias0[h];for(int j=0;j<128;j++)v+=nn_weight0[h*128+j]*f[j];h0[h]=v>0?v:0;}
for(int h=0;h<96;h++){float v=nn_bias1[h];for(int j=0;j<192;j++)v+=nn_weight1[h*192+j]*h0[j];h1[h]=v>0?v:0;}
float v=nn_bias2[0];for(int j=0;j<96;j++)v+=nn_weight2[j]*h1[j];return v;}
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
