#define BOT_TEST
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
static const float nn_w[]={-0.137427181f,-0.0160873178f,-0.0134575544f,0.616704404f,-0.221139506f,0.00872611813f,0.0575622767f,-0.185399532f,-0.0870517269f,-0.0238863043f,-0.143801585f,0.226207927f,-0.0520395413f,0.0374737456f,-0.090190433f,0.05623715f,0.0997708067f,0.183740512f,-0.0713294074f,-0.0646571293f,-0.0715654343f,-0.291331887f,0.052618891f,0.114169858f,-0.178257033f,0.161008447f,0.0210968181f,-0.0580438748f,-0.209401906f,-0.0802157447f,0.0910748988f,0.0699025989f,-0.168071762f,0.117849067f,0.0818693191f,-0.16230911f,0.00864201318f,0.170673743f,0.224318624f,0.0196196791f,-0.0581007935f,-0.0399731584f,-0.0791348293f,-0.190566272f,0.0788017288f,-0.0102384631f,0.0212205257f,0.125691414f,-0.0650057048f,-0.268455178f,0.0400664695f,0.201194376f,-0.148404062f,-0.123771317f,0.0439264886f,0.00190206175f,-0.335220158f,-0.201654449f,0.160868645f,-0.0132731711f,0.224638984f,0.0639573261f,0.0581408665f,-0.499664247f,0.0488377549f,0.0382948481f,-0.00340672047f,-0.0486654937f,0.0641832501f,-0.0542832911f,-0.367528051f,-0.0427375548f,0.124897979f,-0.0549408086f,-0.0841150135f,0.104755558f,0.0740203857f,-0.424813032f,-0.0837766156f,-0.156969205f,0.0150442151f,0.112706639f,-0.0915936828f,0.0402054414f,-0.359650016f,-0.0345005319f,0.166474521f,-0.0982789993f,0.0243379194f,-0.0255057774f,-0.0254933238f,-0.333585709f,-0.00649698684f,0.0464463942f,-0.0807574615f,0.118084542f,0.0759719238f,-0.0539988317f,-0.237750143f,0.231317803f,-0.217440128f,-0.0542121232f,-0.199434817f,-0.087578021f,0.445847154f,0.0187336206f,-0.0513902791f,0.215817451f,-0.276963085f,0.332194477f,-0.0299230758f,-0.0146727581f,-0.525701046f,-0.625860095f,0.0226719808f,0.0430686474f,0.0062214667f,-0.381179541f,-0.00269432506f,0.0114381164f,-0.334594816f,-0.0554866679f,0.0160181224f,0.103249744f,-0.254056603f,0.0636313632f,-0.0767162889f,-0.224333003f,0.208461061f,-0.0224582925f,-0.0598405898f,-0.0714589879f,-0.0344385691f,-0.0797986463f,0.0420044474f,0.394210249f,0.101915203f,0.328216821f,0.15128009f,0.124630414f,-0.071338743f,0.137750581f,0.533535063f,0.0544136353f,-0.00855982956f,0.0497886501f,-0.243480772f,0.0370942801f,-0.0564703234f,0.154479116f,-0.108368754f,0.0531536862f,0.0841015279f,-0.0281577911f,-0.00913287979f,-0.0449545793f,0.28916049f,0.115006343f,0.010802675f,0.136550173f,-0.046739608f,0.384599417f,0.055695612f,0.305814713f,-0.050364919f,0.154856101f,0.13623336f,-0.209363878f,-0.0457166433f,-0.0716201439f,0.233683199f,-0.0330523923f,0.107797667f,-0.0715867206f,-0.138690889f,0.181422323f,0.0607888885f,0.310383171f,0.0851594284f,-0.0102306893f,-0.0913253203f,-0.0303413961f,-0.0683408156f,-0.00343867857f,0.239245787f,-0.164028466f,-0.0622393452f,-0.0424523391f,0.0265359078f,-0.0832668394f,0.00331037305f,0.347632706f,0.0264214184f,0.141168207f,0.0874611959f,-0.012324363f,0.183741853f,0.0727582425f,0.188084751f,0.0468065627f,0.0344542749f,-0.0635455996f,0.246076465f,0.0973753408f,0.0467451885f,0.144787788f,-0.0787040964f,0.0700031519f,-0.149371669f,0.240722612f,-0.182165638f,-0.0165103078f,0.0590667874f,0.00987570453f,-0.0396625623f,0.021717025f,0.206621557f,-0.0997416452f,0.036311198f,0.201519907f,-0.0185178015f,0.274511516f,0.0647244751f,0.23642391f,-0.0205725245f,-0.08414758f,-0.212894157f,0.112819158f,0.0581506528f,-0.152296931f,0.0407143608f,-0.113645129f,-0.406179935f,-0.175926194f,0.405153304f,-0.202486843f,0.419688344f,0.545291603f,0.507124186f,0.0356390923f,-0.644325554f,0.0173455663f,0.0335037783f,0.212149411f,0.00775694428f,-0.236600518f,0.134254768f,-0.0988781601f,-1.4948312f,-0.249827519f,0.00477048941f,0.0523605086f,-0.317481875f,-0.000235265703f,-0.276556611f,0.0816336796f,-0.187369287f,-0.0247995406f,0.00604404649f,0.124911144f,0.0533446446f,0.0390951224f,0.00817921851f,0.0714399219f,-0.051944118f,0.144539684f,0.0371855497f,-0.0400878824f,-0.0360411331f,-0.0157041214f,-0.296253264f,-0.102373391f,-0.0839373916f,-0.0592683777f,0.0754825324f,0.0120935049f,-0.0449407548f,-0.160641506f,-0.00380608602f,-0.124306671f,-0.0994704068f,0.00758417696f,0.110378332f,0.0390901603f,-0.275101125f,-0.0325109921f,-0.0101353526f,-0.0957600474f,0.0573961399f,0.053433802f,0.0735542551f,-0.183496103f,-0.00104171759f,-0.114485726f,-0.121520698f,-0.016605176f,0.0303993169f,0.059901014f,-0.289047509f,0.0240051784f,-0.180848464f,-0.0300195906f,-0.0359387957f,0.0522488095f,-0.0133016258f,-0.105996102f,0.0021361392f,-0.195661098f,-0.0864427909f,0.147824898f,0.181784019f,0.0718527734f,0.281899303f,0.190041468f,-0.160595074f,-0.0560307652f,0.324683338f,0.123255514f,0.0743123442f,0.25510782f,-0.022505071f,-0.191817552f,0.0402313806f,0.0518519618f,0.0797151327f,0.063353315f,0.161391601f,-0.0711474866f,-0.130613729f,-0.0224369802f,0.157680959f,0.0813179314f,0.0218159202f,0.189583361f,0.0376148298f,-0.077753365f,0.00437783031f,0.158941463f,0.179216787f,-0.0267278589f,0.203647897f,-0.132903218f,-0.107171975f,-3.25013825e-05f,0.0423899256f,-0.00394088589f,-0.0295492113f,0.0960914865f,0.0183927119f,-0.289726377f,-0.013151411f,0.210683793f,0.100391358f,0.0170512199f,0.630740523f,-0.154466122f,-0.255386502f,0.200862631f,0.0557980016f,-0.108618557f,0.0468173772f,-0.0660694912f,0.447892219f,-1.02178478f,-0.201695666f,0.103052236f,0.0656657815f,-0.0629569814f,-0.48684305f,-0.913857281f,-0.152246177f,0.280554503f,-0.260569125f,-0.153114989f,0.10746783f,-0.446642697f,-1.2815237f,0.689846992f,0.140252694f,0.0110918097f,0.374936491f,0.0967555568f,-0.260780305f,-0.0503130741f,0.109029874f,-0.0226914454f,0.674590349f,0.224879563f,-0.303237081f,-0.00738539547f,0.0228113122f,0.0225539189f,0.0546212904f,0.472747505f,0.14761351f,-0.0867845267f,0.0218203571f,-0.00279412325f,-0.0226949267f,-0.017761901f,-0.218587503f,-0.183573261f,0.6980564f,-0.100863963f,-0.0616696961f,0.0141111743f,0.119663596f,-0.205034181f,0.12658076f,0.627778769f,-0.138922453f,0.0531880632f,0.0794312581f,0.0310909525f,0.0396962836f,0.247014865f,-0.110478513f,0.171136275f,-0.0278137717f,-0.0628558695f,0.027866073f,-0.031282261f,0.0620134622f,0.395017087f,0.182780191f,-0.00067660067f,-0.0611029752f,-0.0307907667f,0.0150120389f,0.165067181f,-0.086833097f,0.15620783f,-0.0763708428f,0.0355587974f,-0.124112427f,0.202379346f,-0.0611412525f,0.752742589f,0.232518673f,-0.0181018878f,0.0573598817f,-0.0151875075f,0.0266192053f,0.132045045f,0.00439920835f,0.163822979f,0.0261869561f,-0.0604889318f,0.0907787979f,-0.0392571241f,0.056073986f,-0.2420187f,0.155025721f,-0.010802283f,-0.0262498073f,0.0220991615f,-0.11380329f,-0.186612442f,-0.368957967f,0.216997772f,0.0441962518f,-0.0350838378f,-0.12108957f,0.193411291f,0.117211886f,0.203334749f,0.115375578f,-0.0421061888f,-0.0516634025f,0.0934621245f,0.0129658906f,0.297819078f,-0.296307981f,0.0304136332f,0.082139343f,0.0677062199f,-0.161297813f,-0.133433193f,-0.144887239f,-0.260895371f,0.108979173f,-0.0658296496f,0.18665649f,0.0491237752f,-0.119445302f,0.0475054719f,0.172675937f,-0.080604583f,-0.0268467385f,-0.0754668415f,0.147112355f,-0.164703295f,0.188962206f,-0.476314276f,-0.479886383f,-0.273990452f,0.597884238f,0.130905032f,0.144591898f,0.018063914f,-0.113174148f,-0.207630232f,-0.0561324321f,-0.0194178671f,0.838980556f,0.12152189f,0.174087062f,0.135833606f,1.54985452f,-0.0061397776f,0.0215302967f,-0.094874084f,-0.257550538f,0.100440912f,-0.0787341446f,0.524900138f,-0.15984647f,-0.086322248f,0.161578849f,-0.0367335789f,-0.129745513f,0.0118503207f,-0.127797723f,0.192516074f,-0.0757079795f,0.0596094877f,-0.178938314f,0.0428400934f,0.0448508151f,-0.070698306f,-0.120550334f,-0.00828734413f,-0.0783068463f,-0.2094533f,-0.0713350177f,-0.116011456f,-0.158606827f,0.0582070462f,-0.0305553768f,0.0296853557f,-0.100187324f,0.189400509f,0.183083743f,0.136981726f,-0.00250760559f,0.0462166481f,0.0336378776f,-0.158329174f,-0.067344524f,0.101850413f,-0.0456104651f,0.109500386f,0.250888646f,0.00904109236f,-0.252753586f,0.0365568548f,0.180623382f,-0.0267740786f,0.0816469714f,0.0317006595f,-0.0114953453f,-0.0841372684f,-0.0422643237f,0.143474042f,-0.137242496f,0.0075345058f,0.178308755f,-0.0234662816f,-0.309069186f,-0.199887767f,-0.0532029979f,-0.121260323f,0.136112839f,-0.0336797386f,-0.0470861271f,-0.148668796f,0.111716345f,0.144349158f,0.164389819f,0.0202316307f,-0.0626788735f,0.0659732148f,-0.276474506f,-0.0653917417f,-0.0774717554f,0.209410176f,0.192041054f,-0.152914986f,-0.0546867847f,-0.248284712f,-0.0399507061f,0.0449948125f,0.240171686f,0.102751665f,-0.0015670096f,-0.0558220185f,-0.19353956f,0.0913903937f,0.148706764f,0.0219851974f,0.0617169291f,-0.0786747858f,-0.0840893537f,-0.137849733f,-0.043049667f,0.096792452f,0.408923417f,0.601768851f,0.0924595073f,-0.00688053947f,0.00567789096f,0.33047682f,-0.279669791f,0.103726707f,0.007705139f,-0.126439646f,0.246135145f,-0.0218051039f,-0.0650541112f,0.045949623f,-0.283710659f,0.244833171f,0.00188213715f,-0.198154241f,-0.517204046f,-0.653732359f,0.223051652f,0.0798868462f,0.00867675617f,-0.0539090447f,0.13264364f,-0.0120975999f,-0.333683878f,-0.197100073f,-0.0146632455f,0.0722270608f,-0.144343108f,0.00634608557f,-0.201363251f,-0.196324423f,0.678975284f,0.140472159f,0.0584189147f,0.185321733f,-0.116083182f,0.0680254847f,0.0427854545f,0.259579539f,-0.03906665f,-0.113610595f,-0.0980188847f,0.034609884f,-0.0628311262f,0.0906627402f,0.165169224f,-0.287392825f,0.0538426228f,0.110321961f,-0.106386222f,0.091877155f,-0.00518488558f,0.0602272488f,0.0579021759f,-0.0111831371f,0.147949547f,-0.255654424f,0.073671706f,0.0460976027f,0.264787912f,0.176147625f,0.0776231661f,0.101250388f,-0.0394852236f,0.150232479f,0.0453147963f,-0.0827728957f,0.0274203308f,0.0836161599f,0.212803766f,-0.111461088f,0.0553033613f,0.0525050312f,0.295450062f,-0.0926855057f,0.0402141958f,0.00313867605f,-0.0466299802f,0.0731296539f,0.0307296496f,0.223295137f,0.202374294f,0.114845149f,0.101192325f,-0.0435654074f,0.0324715674f,-0.0100836791f,0.144765034f,0.0477148071f,-0.0143211763f,0.0539944395f,-0.0870397463f,0.00668006437f,0.0425185002f,0.213785917f,0.0324097984f,0.0995806456f,0.134025201f,0.149019331f,0.0279798228f,0.0727070719f,0.0920730308f,-0.0302363634f,0.134522766f,0.0777453855f,0.00262592291f,-0.0659704506f,-0.0712213591f,0.217356578f,0.131697252f,-0.0267262775f,0.0646554232f,-0.207318842f,-0.0627904907f,-0.0231979154f,0.106326707f,-0.132450148f,0.108277611f,-0.0202962607f,0.0974241942f,-0.0739420429f,-0.0471512005f,0.0619719997f,-0.0362818204f,-0.0470708348f,-0.041880697f,-0.0593438782f,-0.0810710117f,0.0807070807f,0.183229089f,-0.35253948f,0.118042767f,-0.0347285084f,0.717749715f,-0.171468183f,-0.189190567f,0.000346161047f,0.266816914f,0.0965252742f,0.00533416076f,0.118120268f,-0.756267905f,0.510187328f,0.7075091f,-0.600224257f,0.16457738f,0.130606711f,-0.299848884f,-0.2836155f,-0.166110173f,-0.10046497f,1.00413609f,-0.352683783f,0.123129971f,0.015625326f,0.161358953f,-0.114278123f,0.430232316f,-0.319665253f,-0.110831819f,0.0136701809f,0.429809421f,0.046237845f,0.13544324f,-0.0295685139f,0.0105865411f,-0.105075523f,-0.173755571f,0.35118264f,0.163165957f,-0.104711816f,0.0090029072f,0.103877917f,-0.152829513f,-0.0683502406f,-0.00281221932f,0.400483102f,-0.0642442331f,-0.210118324f,0.0510473102f,-0.0912164375f,-0.115117542f,-0.0105215581f,0.107555084f,0.0859474167f,-0.141001806f,0.00057715521f,-0.0335815065f,-0.0327251442f,-0.0974185988f,0.132128581f,0.0134367766f,-0.180842653f,0.0458435081f,-0.225670487f,0.0177362654f,-0.195580959f,0.173870549f,-0.0275980588f,-0.25939393f,-0.0346003324f,0.0172299575f,-0.0317095369f,-0.0454646088f,0.472025514f,-0.058195211f,0.138408005f,0.0159463305f,-0.0740047321f,0.0701005384f,-0.14685069f,0.14587377f,0.252891809f,-0.0909436569f,0.0268687438f,-0.0323089212f,-0.0263632052f,-0.0999384448f,0.281096935f,-0.200048313f,0.137311801f,0.0561042242f,-0.129246444f,0.0944929495f,0.103909142f,0.168248042f,-0.299502254f,0.456965536f,0.0470720083f,-0.132569849f,-0.0369855016f,0.349437952f,-0.103596024f,0.155122116f,0.00808329042f,-0.0505509153f,-0.0821429491f,-0.110789478f,-0.0803123862f,-0.31079185f,-0.239793837f,-0.123670563f,0.0685812309f,-0.121305868f,0.0811794475f,-0.2273743f,0.0503920801f,-0.155692294f,-0.0265841857f,0.0507268235f,-0.179190382f,0.0621280707f,0.033962559f,0.20606862f,0.0554409847f,0.0925599858f,-0.0146931456f,-0.62873131f,0.143606722f,0.0769841447f,-0.0336094201f,0.0305106379f,-0.00852544419f,0.166802928f,0.00912431255f,0.0533388965f,0.00573194213f,0.473987848f,0.381822467f,0.357085109f,-0.15781945f,-0.493576288f,0.0463601947f,0.238262117f,0.205610797f,0.346859843f,-0.284437209f,0.0194755364f,-0.275290132f,-1.10484755f,-0.170701891f,-0.0374965295f,0.11121285f,-0.555065632f,-0.0391113609f,-0.010842992f,0.155760556f,-0.323872924f,0.0989797562f,-0.0821230412f,0.0329419076f,-0.0972538367f,-0.0765775815f,0.144289419f,-0.156250596f,0.0394481607f,0.318765819f,0.58667922f,-0.0142469527f,0.0551552884f,0.173318654f,-0.159616292f,-0.00292045879f,-0.0128343971f,-0.03482721f,0.172943875f,0.021808831f,-0.0145134954f,-0.21653156f,-0.167694315f,0.0715880319f,0.0471491814f,-0.0199140124f,0.0865923837f,0.0443774946f,-0.303131908f,-0.00350967958f,0.109874845f,0.133078322f,0.445687562f,0.16930826f,0.0298228059f,-0.252311677f,0.125073254f,-0.137871742f,-0.0628048927f,0.501893878f,-0.0375737734f,0.0758401603f,-0.225771993f,-0.0396427959f,-0.239368662f,-0.0604968406f,-0.0124583775f,-0.10195902f,0.0144329835f,-0.383481592f,-0.0888419151f,0.0333619751f,0.0630126894f,0.415744305f,0.0022333276f,0.0716640055f,-0.234374717f,0.0381497182f,-0.0353442095f,0.362037241f,-0.134097755f,-0.121636145f,0.0358065851f,-0.150059193f,0.0758194327f,-0.0161396638f,0.646095157f,0.0249841623f,0.0379540361f,-0.0494843237f,-0.288962632f,-0.0225635991f,-0.193334073f,0.555368483f,0.00175606494f,-0.0992441922f,-0.0224058721f,-0.199086577f,0.0125522269f,0.0279573835f,0.342175901f,0.105308644f,0.0669800267f,0.052634012f,-0.231420845f,-0.238167584f,-0.0463372096f,-0.0107592111f,-0.0426164828f,-0.0309137609f,0.00190251484f,-0.245104089f,-0.0395335183f,0.0397601537f,0.458309561f,0.044082813f,-0.0135137485f,0.0311467759f,0.0873505101f,0.332640082f,-0.27796942f,-0.255014956f,-0.150798634f,-0.0742555559f,0.120714471f,-0.0101935202f,-0.287811011f,0.277324617f,0.0487512834f,-0.541886091f,-1.0951941f,0.292369366f,0.530449569f,-0.460911274f,0.174368426f,0.143681481f,-0.0833023489f,-0.219721064f,0.0803582743f,0.155297443f,0.410803974f,-0.0644132346f,-0.0285152066f,-0.148861453f,0.262910783f,0.124578245f,0.308260202f,0.137915194f,-0.196254209f,-0.00295684254f,0.161774293f,0.0572752543f,-0.0488533936f,-0.0189660452f,-0.119348742f,-0.153064191f,-0.0498977304f,0.282004178f,0.17310974f,-0.242410779f,-0.00778600108f,0.0298572332f,-0.20685868f,0.117142603f,-0.120701469f,-0.128411338f,0.0246900953f,0.0367912054f,-0.0661373213f,-0.236324921f,-0.033136677f,-0.198092088f,0.00309750065f,-0.0486210398f,-0.16876778f,-0.0485430695f,-0.0121149365f,-0.115672216f,0.0152645977f,0.131398216f,-0.175538927f,-0.0395402759f,-0.0549815483f,-0.280348867f,-0.067596972f,-0.0509879068f,0.0998351201f,0.0104206661f,0.0806878433f,-0.0330098048f,-0.036386773f,-0.0961650014f,-0.20761171f,0.128989667f,-0.136055693f,0.0956785157f,-0.0261176564f,-0.318266571f,-0.0399594307f,0.106217772f,0.210613072f,-0.034510497f,0.0655532628f,0.0633803532f,-0.143914402f,-0.00739123346f,-0.0072208331f,0.489452094f,-0.372386277f,-0.045067843f,-0.0257840212f,0.0150941182f,0.134478718f,0.0683257654f,0.0944403559f,-0.28437835f,-0.0696074143f,-0.0444728099f,-0.0312529206f,-0.0776191503f,-0.028021168f,0.463930577f,-0.226964965f,-0.111996397f,0.00818739738f,-0.119775198f,0.0856755972f,0.119761139f,-0.0131078037f,-0.3348746f,0.013088366f,-0.0429548062f,-0.181531623f,-0.0534983017f,0.0765326247f,0.271840125f,-0.320629179f,0.0341441818f,0.0176761094f,-0.333610117f,0.195015624f,0.137560323f,0.275233805f,-0.10622219f,0.156748086f,-0.0298680719f,-0.507483363f,0.224224806f,0.0282094013f,-0.267871767f,0.0797991529f,0.135821417f,0.121779181f,-0.142451182f,0.277033478f,0.052527871f,0.498655796f,0.0830567181f,0.422890782f,-0.200585797f,-0.476999193f,0.0905794278f,0.349106371f,0.414618433f,0.103230536f,-0.174504846f,-0.360096872f,-0.365578383f,-1.08577609f,-0.200281978f,-0.022965448f,0.128267929f,-0.509229064f,-0.0555744134f,0.00110996095f,-0.0787387937f,0.134587824f,-0.0794334114f,-0.0564950854f,-0.0739644542f,0.0561853088f,0.00287693948f,-0.0731679872f,0.555425704f,-0.106896952f,0.0126803918f,0.278249651f,-0.0636874512f,0.0828524083f,0.266282439f,0.327479839f,-0.0238450412f,-0.0560715348f,0.0448368974f,-0.0463660061f,0.0213625487f,0.0367804281f,0.161720827f,-0.0360539034f,0.0306159854f,-0.0161336921f,0.173296258f,0.00771533139f,-0.0673161745f,0.291973472f,0.0479168147f,0.0629034787f,0.0288272817f,0.107600041f,-0.0559077859f,0.00387985073f,0.28878364f,-0.0814734623f,-0.0466814712f,-0.0641061217f,-0.0369116664f,-0.0251864474f,-0.0224747509f,0.234134346f,0.0245355871f,-0.0213151127f,0.145454496f,0.100476995f,0.0669513941f,-0.0784296319f,0.317962348f,0.0101688877f,-0.0471928269f,0.0632182881f,0.277235091f,-0.143676564f,-0.0159975495f,0.120330282f,0.0426080897f,-0.19307451f,0.318021119f,0.272691667f,-0.108311638f,0.0622841641f,0.478811562f,-0.00254767644f,-0.0491882861f,0.285582811f,0.234053746f,-0.0455752052f,0.0284770895f,0.44156599f,-0.215993315f,0.0565049127f,0.181148633f,-0.0823163614f,-0.0572025962f,-0.0882634893f,0.191558138f,0.0577102005f,-0.0582456663f,-0.103988804f,-0.09054479f,-0.0186664537f,0.0652678162f,0.579331756f,0.118160546f,-0.0118075898f,-0.0934730545f,0.470959961f,0.0221762881f,-0.0880196318f,0.17142348f,0.138169989f,-0.0653039068f,0.00864299573f,0.338314444f,0.0711066499f,-0.0437950343f,0.169801116f,0.154049143f,-0.197394997f,-0.178579479f,0.683882177f,-0.132241771f,-0.0929142982f,-0.0120488554f,-0.175496086f,0.106300876f,0.0827240422f,-0.712026477f,-0.284758985f,0.0763031319f,0.570920289f,0.533191025f,0.177952886f,-0.0873467252f,0.0653819591f,-0.105957106f,0.258733958f,0.0458717756f,0.796387136f,0.511302292f,-0.0113833435f,-0.101310976f,0.0523838103f,0.0874032006f,-0.0389597826f,-0.0609553419f,-0.0502906144f,0.0340438001f,0.122070201f,-0.0532864183f,-0.108586349f,-0.00394048961f,-0.0513144806f,0.748426318f,0.1532107f,-0.0513167083f,0.0957156867f,0.0243010316f,0.074439019f,-0.0855599195f,0.322281599f,-0.118738055f,0.0760012493f,-0.101473533f,-0.0559765212f,0.0712938309f,0.0485227332f,0.134127423f,-0.0823922902f,0.0690515786f,0.0719637275f,-0.0240962785f,0.149038464f,-0.0180189647f,-0.0659114644f,-0.0228559282f,0.0976531059f,0.0900845081f,-0.112942562f,0.0840937421f,-0.0453974269f,0.0581473708f,-0.0464413837f,-0.0756326765f,0.138936907f,0.00557897054f,0.157417715f,0.0316538773f,0.158878326f,-0.0165460296f,0.0141175473f,0.143426344f,-0.0679359362f,0.002145292f,-0.0823880732f,0.0136650922f,-0.0101424949f,0.135360911f,0.022658756f,0.00103567285f,0.236841232f,-0.0346398242f,0.14834778f,0.0852594525f,0.0664985999f,0.144979253f,-0.00165295496f,0.0294358265f,-0.00675846124f,0.144344315f,0.0404990464f,0.00365339359f,-0.0695265308f,0.116065264f,0.0275631715f,0.0548024662f,0.180412099f,0.00759221567f,-0.1089788f,0.0524986163f,-0.141401142f,-0.107609376f,0.0513484068f,0.0813636109f,-0.191806883f,-0.139769405f,0.0481197983f,-0.0465967581f,-0.0902258903f,-0.0198872387f,0.271328986f,-0.0221338533f,-0.1168603f,0.0354136378f,-0.0432730243f,-0.226057842f,0.0249760244f,-0.0453851335f,-0.102979712f,0.0250928253f,0.157353386f,0.129002199f,-0.0494890511f,-0.0464612655f,-0.0736120418f,0.123403095f,-0.181531504f,-0.0020997317f,0.88513273f,-0.148809478f,-0.195170581f,-0.0527670905f,-0.448618084f,0.0450224541f,0.00384051399f,-0.463978082f,-0.0870886967f,-0.544915557f,0.431497902f,0.597579837f,0.0444194861f,0.064605467f,-0.188719884f,-0.10040883f,-0.117305391f,-0.0844571143f,1.01159334f,0.73417896f,-0.0222551022f,0.0142653203f,-0.182190523f,-0.0407087468f,-0.220146596f,-0.374540448f,0.00528391963f,-0.0196438059f,0.215276405f,0.186725944f,0.108781964f,0.084077552f,0.173845172f,0.147885054f,-0.00787646323f,-0.117023595f,-0.0128107183f,0.509267151f,0.00680479081f,0.190543085f,0.217189252f,0.113488644f,-0.168405667f,0.0657078773f,-0.0257826671f,0.0602419935f,0.00392524293f,-0.0577185676f,-0.0859252587f,0.199911073f,0.376790732f,-0.0365917012f,0.532783508f,0.0569996536f,0.134810522f,0.0387112796f,-0.0297559947f,0.0765925273f,-0.0529790446f,-0.319227874f,0.0697793067f,0.209191024f,0.0736837387f,0.159164548f,0.408011258f,-0.129410759f,0.170938164f,-0.0118274344f,0.121855617f,-0.0673536137f,0.0329517648f,0.529572904f,-0.114438824f,-0.122576989f,0.0777384415f,-0.0122138327f,0.0559458174f,0.131839558f,-0.0886839181f,-0.191573739f,-0.0447413847f,0.0611738488f,0.043293979f,0.00252619991f,0.210132763f,0.226771563f,0.0841230974f,-0.0471782573f,-0.0326788574f,0.042369023f,0.0240904503f,0.646533966f,0.162064582f,0.283417493f,0.521798909f,0.0260696728f,0.180663675f,-0.00358281448f,0.00455255387f,0.200514734f,0.296428442f,-0.0929299444f,-0.0692610368f,0.0987893194f,0.0475729108f,-0.114633091f,0.09626472f,-0.00723948423f,-0.350922883f,-0.00909221638f,0.153479636f,-0.0143180499f,0.141193971f,0.171759829f,0.114460677f,-0.180860728f,-0.0333355144f,0.0894598514f,0.100076571f,0.205494791f,0.411338896f,-0.255481958f,-0.106945433f,0.0517864488f,0.223706186f,-0.0910210758f,0.183476537f,0.0525508262f,-0.131337464f,-0.102706f,-0.111433022f,0.0204588491f,-0.162052453f,-0.0811863542f,0.173004925f,0.217586398f,0.137359977f,-0.207322985f,-0.482323378f,0.0282210615f,0.285787195f,0.30835709f,-0.254380435f,0.0108599216f,0.0184537433f,-0.0104004852f,-0.764867544f,0.0562492274f,-0.147652537f,-0.176992148f,-0.830204666f,0.0927764997f,-0.0759014264f,0.0594089217f,0.140592545f,0.0220455006f,-0.127692789f,-0.0579469912f,-0.0744065568f,-0.0288296156f,0.00216491427f,0.0394726098f,0.0841232985f,-0.0834790394f,0.860517919f,-0.144287944f,0.070599094f,-0.0222152993f,0.227537706f,0.0220178775f,0.0333074965f,-0.0204499401f,0.105018131f,0.0157084651f,-0.044311028f,0.133295491f,0.0691694692f,0.195921361f,0.0854804739f,-0.0887684897f,0.0119598657f,-0.0644029379f,0.352968663f,-0.173647106f,-0.0510620587f,0.147809103f,0.374941289f,-0.0189378839f,0.0732935518f,0.2494708f,0.115781762f,-0.00705486629f,-0.0105375722f,0.156680673f,0.0084356796f,0.0356988348f,0.305530012f,0.0884790123f,-0.102255568f,-0.0740324855f,0.44949615f,0.0421014093f,0.00162707467f,0.237496689f,0.026105132f,0.0667926446f,0.00835489854f,-0.267533779f,-0.0332755819f,0.0496584214f,0.0403730124f,0.111798786f,0.136618987f,0.240090981f,-0.0733124539f,0.170129925f,-0.0183661487f,0.151064619f,0.049713105f,0.300869763f,0.323998481f,-0.0293952767f,0.0992109403f,0.0243633464f,0.295743197f,-0.0738456175f,0.291721374f,0.146984801f,-0.0893105641f,0.0603516214f,0.00517360121f,0.105606951f,0.0227990132f,0.216870531f,-0.0908772722f,0.00556369359f,0.20232062f,0.0502358973f,0.0689779967f,0.0711348951f,0.235372066f,-0.0365299843f,-0.135968938f,0.00120064721f,-0.0213177446f,0.159732804f,-0.12553528f,0.144004539f,-0.0565381534f,-0.0171832684f,0.11521633f,-0.0485814027f,-0.0515161231f,0.172128543f,-0.337522149f,0.00152300543f,0.447915643f,-0.0150419157f,-0.190412328f,0.0337678567f,-0.0522983447f,0.369754583f,-0.387590945f,0.460702837f,-0.372274429f,-0.135603294f,-0.426151842f,-0.633135557f,0.113958307f,0.00440429291f,0.0552878864f,-0.347254097f,-0.0112679005f,-0.063436158f,-0.0113868099f,-0.126014203f,0.00596368918f,0.240482748f,-0.441421628f,0.0287226457f,-0.0151686296f,0.474849194f,0.272283167f,0.130024061f,0.0368785709f,-0.0456694923f,0.0160687398f,-0.040613506f,0.0124259945f,0.089713946f,0.0320482962f,0.340489954f,0.366901159f,0.0396321528f,0.0195489582f,0.240035459f,0.35730806f,-0.202902377f,-0.0223575737f,-0.105241254f,-0.0461776741f,0.231374905f,0.0249786489f,0.134898603f,0.0412362218f,0.174440339f,0.058762636f,0.352272481f,-0.00967431441f,0.0661384687f,0.198256373f,0.193870321f,-0.059998706f,-0.14393416f,0.171221495f,0.248509988f,0.0713264197f,0.240247503f,0.0187033731f,0.0462274142f,0.0194267072f,0.327533662f,-0.00523865735f,-0.0633117184f,0.169713706f,-0.0591547415f,0.0694288015f,-0.234493554f,0.594589949f,-0.305795312f,0.000628974754f,0.22162579f,0.0495179705f,0.0879591182f,-0.0549810603f,0.373437554f,-0.136769623f,-0.0470403358f,0.235867098f,-0.0986232385f,0.1801209f,0.281017214f,0.0239135325f,-0.255267501f,0.0557844043f,0.351312011f,0.0178161804f,0.153845057f,0.240599796f,0.10456223f,-0.00889716391f,-0.076769501f,0.272101998f,0.035012234f,0.0611604117f,0.258128732f,0.263984144f,0.301466107f,-0.0599696189f,0.131319538f,-0.115658887f,-0.0714406148f,-0.0195104908f,0.394221097f,-0.0171570163f,0.0633941144f,0.161534786f,-0.137354299f,0.144765571f,-0.254188061f,0.0862612277f,0.162051767f,-0.0757782757f,0.146545887f,-0.110951439f,-0.0244614854f,0.818079114f,0.037215367f,0.100953825f,-0.0567804277f,-0.386760056f,0.143081516f,-0.00335663697f,-0.0194363836f,0.151567549f,0.0371979401f,-0.180818394f,-0.064950496f,-0.34937638f,0.104974627f,-0.0766560808f,0.491356462f,0.323666245f,0.160621122f,-0.686856508f,0.369151294f,0.0315674543f,0.167691857f,0.0162659679f,0.0234468244f,0.252121657f,0.241767064f,-0.789866865f,-0.112792872f,0.0275868401f,-0.0248833988f,-0.59835273f,-0.00878233556f,-0.0579466224f,0.22866343f,-0.414702892f,-0.117199309f,-0.116128579f,0.0619645454f,-0.127700523f,0.0843873844f,0.00107963814f,0.0941439793f,-0.0282380451f,0.069501847f,0.0823607147f,-0.0371130221f,0.0857754499f,0.015067352f,-0.0358204693f,0.0877363011f,-0.0399356224f,-0.0460006632f,0.0159965195f,0.137553498f,0.055794023f,-0.106104761f,-0.187421292f,0.1868909f,0.242241204f,-0.197010979f,0.0598321632f,-0.0247750897f,0.0128278984f,-0.0737194866f,0.14740327f,0.319124192f,-0.0695265457f,0.033736743f,0.0838517994f,-0.125545263f,-0.0821104944f,-0.136887804f,-0.0417329594f,-0.117425472f,0.0969056338f,-0.0622097217f,0.0594314635f,-0.125975743f,-0.0308003929f,0.259501487f,-0.21470046f,-0.0368135422f,0.0879581496f,-0.0619958937f,0.0830701739f,0.141884968f,-0.0447191671f,-0.0752254575f,0.171116754f,-0.0644682795f,-0.120678261f,-0.0735572204f,-0.114097275f,0.167852193f,0.413716644f,-0.0875302777f,0.0522954129f,-0.358995736f,-0.0185712259f,0.130491748f,0.174972996f,-0.0370324068f,-0.0679226816f,-0.0457676873f,-0.197026029f,-0.167742133f,0.210208029f,0.00619280525f,-0.0829506069f,0.0553547554f,0.00379593647f,-0.294962049f,0.102413163f,0.119983189f,-0.0956815779f,0.260475427f,-0.162857637f,0.0785163343f,-0.299457252f,-0.0571246222f,0.216636583f,-0.141471922f,-0.052757863f,-0.139540121f,0.0669593066f,-0.0397775173f,0.0490198433f,0.051196564f,0.188179567f,0.210897073f,-0.0516590476f,0.0605771542f,-0.0295599755f,-0.096091643f,-0.107242323f,-0.0269137248f,-0.265627891f,-0.092809394f,0.0863600671f,-0.0597527251f,0.0240383968f,-0.606342137f,0.000490103266f,-0.079872556f,0.187249586f,-0.120814413f,0.708369732f,0.555375338f,0.107191615f,-0.100019142f,-0.223857448f,-0.429383785f,-0.142938361f,0.0716893375f,1.86715734f,0.621704102f,-0.131096497f,0.0814607963f,0.097870402f,-0.139430478f,-0.295246899f,0.042869322f,-0.170062065f,-0.139142275f,-0.109379604f,0.411464691f,-0.124380246f,0.00747676985f,0.130962655f,-0.224944904f,0.0905441716f,0.00198018691f,-0.169373602f,0.195782229f,0.0547639318f,0.0358627513f,-0.177916124f,0.117150098f,0.232739955f,0.223601475f,-0.0525824726f,-0.116842255f,0.0584693365f,-0.258237451f,0.0108318552f,0.324045986f,-0.0312825814f,-0.186440289f,0.125691175f,0.0609558634f,-0.176128507f,-0.077579692f,0.0377127305f,-0.14758268f,-0.0696452856f,-0.199259177f,0.0511825681f,-0.0590124279f,-0.0539843924f,0.31307748f,0.142986476f,-0.0595895611f,0.11048913f,0.0803699642f,-0.0516432896f,-0.0037553052f,0.107832961f,0.145844772f,-0.0408032201f,-0.0404019691f,-0.0386242047f,-0.239367187f,0.063904494f,0.2213673f,0.158971444f,-0.149068862f,0.16777809f,0.0531225465f,-0.337529272f,-0.0436595641f,0.133723691f,0.15248397f,0.187272519f,0.149169624f,-0.0612708256f,-0.450382501f,-0.0478856936f,0.0361340493f,0.0283755511f,0.0905943587f,0.049461633f,-0.0331960283f,-0.451680958f,0.0218342394f,0.176926926f,0.0128090708f,0.024832299f,0.123237386f,-0.0848295465f,-0.251567125f,0.00364601589f,-0.0575193875f,0.0785327181f,0.367836356f,-0.146630019f,0.0437330678f,-0.41342169f,-0.0692048892f,0.215872616f,-0.0416870229f,0.281107306f,-0.0670248866f,0.0274816751f,-0.42346096f,0.0227288119f,-0.0397874564f,-0.00545772351f,-0.138249546f,0.0446781702f,0.00511093903f,-0.334451318f,0.260248959f,-0.403609037f,-0.04019529f,-0.295024335f,-0.0443864726f,0.377444834f,-0.140607446f,-0.0779260322f,0.151029915f,-0.269877762f,0.25826174f,-0.0616622381f,-0.0611749329f,-0.520788312f,-0.426149368f,0.138129145f,0.0750529096f,0.150808498f,-0.246261552f,-0.0562991686f,0.165945783f,-0.546157062f,-0.396919847f,-0.067163229f,0.123294786f,-0.414886653f,0.0269423127f,-0.246332705f,-0.180123657f,0.103708118f,-0.0118273469f,0.138645798f,0.218806908f,0.156002641f,0.046410393f,0.217720106f,0.0334010608f,-0.0180710703f,0.475265294f,0.186041236f,0.387033701f,-0.0230501071f,0.416873902f,0.0158677585f,-0.03786413f,0.153581724f,0.231890082f,0.00519514643f,-0.0338188373f,0.000914368371f,-0.0575174876f,0.00746259838f,0.389160305f,0.384826243f,0.302906573f,-0.00152846309f,-0.0851542354f,-0.00266239396f,0.0214446541f,0.488337725f,0.144448221f,0.0659779236f,-0.0572266951f,0.00018205501f,-0.0052360855f,0.0515277609f,0.101906896f,-0.141195118f,-0.116082139f,0.0838899687f,-0.0761348158f,-0.0208723862f,0.044876758f,0.142286405f,0.314872146f,0.250062317f,-0.034950994f,0.0219884273f,-0.0879559144f,0.00441044848f,0.200841337f,0.697233498f,-0.00622319616f,0.0427985191f,-0.00481774472f,-0.049972754f,-0.0512598604f,0.137512654f,0.0762245506f,0.245663613f,0.090056479f,0.0419915728f,0.0850033984f,-0.0450079478f,-0.00100014184f,0.213340208f,-0.195057347f,0.113313571f,-0.0352115966f,0.157510206f,-0.0494191684f,0.139101774f,0.0664481372f,-0.057939399f,0.0367017426f,0.00614932319f,0.0483602397f,0.0286218673f,0.183790818f,0.320240587f,0.0540272333f,-0.0930511057f,0.0106096715f,0.00969769526f,0.00477921544f,0.142287403f,-0.0291678905f,-0.00301683997f,-0.0446095094f,0.024315415f,-0.159220472f,-0.11847315f,0.165637061f,0.28120026f,-0.189226463f,-0.0442103632f,-0.0041812323f,0.377809644f,0.090296641f,0.00392703386f,-0.140889093f,0.145790994f,0.0447754487f,0.0981887281f,0.0546690077f,-0.291842699f,0.0912995413f,0.107120551f,0.000225101991f,0.409885049f,-0.432966113f,-1.74649763f,0.57245332f,0.347093761f,0.408599854f,-0.0557637103f,-0.351336241f,0.125043139f,0.203954965f,-0.85517782f,1.24493599f,0.0540646911f,0.0188148413f,-1.78421557f,0.21464467f,-0.234194711f,0.0393036939f,0.166408747f,0.0440909117f,-0.108760998f,-0.110431172f,-0.025408389f,0.0767593011f,-0.0380808339f,0.286764264f,-0.147497565f,0.22532925f,0.223212346f,-0.18813549f,-0.0237530489f,0.10299857f,0.382735223f,0.0200026669f,-0.0323276259f,-0.210085243f,0.444777042f,0.0351721831f,0.0127268061f,0.215307027f,0.13764137f,-0.0451432019f,-0.221024245f,0.190383077f,0.116767451f,-0.0246240981f,0.201705724f,0.0549929291f,0.102659047f,-0.122625105f,-0.231479049f,0.198083594f,0.0474013947f,0.5220927f,0.0610589944f,-0.0972383544f,-0.221699804f,0.00164241483f,0.15898703f,0.0567537919f,0.179874226f,0.0843619183f,0.103665076f,0.246672094f,0.167875171f,0.00862563029f,-0.00630480284f,0.354050487f,0.0940639749f,-0.151670054f,-0.0648012459f,0.0771226957f,-0.0294563882f,-0.00509450166f,0.279584646f,0.0295596626f,0.182134598f,0.247280777f,0.197401777f,-0.058487948f,-0.0173454154f,0.210379422f,-0.0278972741f,0.208055839f,0.136929125f,0.039049916f,0.00977612846f,0.0109046679f,0.268481195f,-0.178136259f,0.169408783f,0.11608097f,-0.326781899f,-0.00152550742f,0.0745496005f,0.0626521558f,-0.132259876f,0.18437247f,0.195032656f,-0.0364654176f,-0.100318335f,-0.0203222781f,0.109432466f,-0.159201026f,0.29546386f,0.265087456f,-0.0282121804f,0.047492031f,-0.0482452065f,0.131377503f,-0.0862260461f,0.143665403f,0.055694852f,0.0737212971f,-0.117242299f,-0.0396911129f,-0.149860263f,0.115572326f,-0.149969101f,0.0928533226f,0.681284428f,-0.159824044f,-0.381187201f,0.113404661f,-0.261956334f,0.462547511f,-0.243735686f,0.53485626f,-0.179958805f,0.203092486f,-0.55357641f,-0.637094855f,-0.0121802967f,-0.173111767f,-0.117457099f,-0.258574635f,0.0602094568f,0.146976963f,-0.0653565452f,-0.230781958f,0.137112021f,0.216708064f,-0.309731632f,0.0151737556f,-0.0603838786f,0.424587548f,0.295007944f,-0.0443621613f,-0.0814473182f,-0.130382553f,0.159413144f,-0.0406915843f,0.0539517514f,0.145691305f,-0.0755048767f,0.111442082f,0.62800926f,-0.1842978f,0.0296892952f,0.14847526f,0.306267291f,-0.0278834105f,0.00653791986f,-0.0450809784f,0.159290507f,-0.0252001882f,-0.0183884874f,0.32342732f,0.101067141f,-0.0340706334f,-0.056339521f,0.361076385f,-0.166983783f,-0.0857334882f,0.309874743f,-0.132956102f,0.00248456374f,0.0054501947f,-0.122206628f,0.110788308f,-0.0388687737f,0.399299771f,-0.057377737f,-0.0930521637f,0.0542945042f,-0.0519393869f,-0.146620959f,-0.0650701374f,0.206435725f,0.0263862461f,-0.12018916f,0.00390431285f,0.16065453f,0.00400891621f,0.0510856099f,0.248962909f,-0.0464181751f,0.0677076057f,0.00280453218f,0.169893682f,0.0125739072f,-0.0664558783f,0.16318576f,-0.0311391894f,0.19343926f,0.186638817f,-0.139759168f,0.0869359672f,-0.00488418899f,0.206998527f,0.131607682f,0.224739388f,-0.193121418f,0.0719896629f,0.144997567f,-0.00361895119f,0.351637781f,-0.00894646067f,0.301450253f,0.173777193f,0.165970042f,0.0704462305f,-0.0723451301f,0.0504126586f,0.155841261f,0.245886222f,-0.0940570906f,-0.0409219451f,0.227246374f,-0.085905157f,0.298446298f,-0.0461948551f,0.0260015465f,-0.189149022f,-0.0402422585f,-0.0536999367f,-0.0782376751f,0.29554227f,0.0439825729f,0.174908862f,0.173501104f,-0.157942414f,0.152702719f,-0.0340568274f,-0.20405288f,0.201900452f,-0.0358836167f,0.0686888173f,0.225779787f,-0.0592043027f,-0.322569937f,-0.00402439805f,-0.0417004824f,0.226237595f,-0.33246699f,0.458175778f,0.0019847292f,0.0335685238f,-0.546949565f,-0.988943517f,0.108538762f,-0.135327622f,-0.303980142f,-0.17720072f,0.0501892678f,0.0370389558f,-0.56964916f,-0.195985764f,-0.043535281f,0.245168895f,-0.517433941f,0.082501404f,-0.0548168197f,0.210304722f,-0.594738841f,0.0332805365f,0.00271878694f,-0.00449553551f,0.0518600978f,0.0590202175f,0.143870965f,-0.158701628f,0.0884852409f,0.244078621f,0.0413766578f,0.0351064168f,-0.00530031556f,-0.0302274302f,-0.127848029f,0.239258096f,-0.0602110736f,0.124248832f,0.248318732f,-0.0045327181f,-0.00945470016f,-0.34670639f,0.0310087465f,-0.140818954f,-0.0249420702f,0.123437911f,-0.0147530334f,-0.0201669149f,-0.282129854f,0.0330465697f,-0.0030848817f,0.00288593397f,0.577460647f,-0.0540549345f,0.0857659355f,-0.169226587f,0.0540440343f,-0.161728248f,-0.0364993997f,0.196675077f,0.0600598305f,0.0804007724f,-0.0836376324f,0.0141761582f,-0.0581310987f,0.133265674f,0.126601711f,0.152873531f,0.0198507719f,-0.353549898f,0.0750162154f,-0.0675241053f,0.0587957986f,-0.0870573446f,0.201101556f,0.0390930288f,-0.175307214f,0.034645997f,0.00608466053f,-0.187297583f,0.0458364561f,-0.0437778197f,-0.069234699f,-0.177445129f,0.156075478f,-0.0649280474f,0.349660873f,0.0769183114f,0.0122161498f,0.0353999287f,-0.200643659f,-0.0956092179f,-0.0241968725f,0.213142902f,-0.0893720165f,-0.0335230939f,0.042104207f,-0.389980823f,-0.0106259705f,-0.0051584309f,0.264265478f,0.0193981007f,0.102334388f,-0.0825195834f,-0.198783308f,0.092516005f,-0.013094658f,0.178210974f,0.0364170894f,-0.00383378915f,0.02910964f,-0.0652025193f,0.209933296f,-0.0327939764f,0.0181623939f,-0.0579100139f,-0.08528395f,0.0332019515f,0.0260674804f,0.254425049f,-0.160758927f,-0.279664367f,-0.236807778f,0.104230978f,0.129508078f,0.0125309825f,-0.32073167f,0.161543533f,0.00564413518f,-1.09349084f,-1.30324423f,-0.23816821f,0.567139685f,0.106160946f,0.156883672f,-0.0802914426f,0.142693013f,0.176522359f,0.00986345112f,0.151079968f,0.406128585f,0.283879876f,-0.00311268098f,-0.00392380403f,-0.0703799427f,0.140082464f,0.559791088f,0.157067388f,-0.00981147215f,0.0136154145f,-0.0279093795f,0.166017622f,-0.096494481f,0.056056682f,-0.0166251771f,0.0727434382f,-0.206131548f,0.186018646f,0.254264861f,-0.0635570437f,0.0214159582f,0.006049369f,0.0771638602f,-0.224174961f,0.174158409f,-0.0521593988f,0.369853824f,-0.276404858f,0.0644232109f,-0.180812478f,0.013432499f,-0.111706935f,-0.0976052359f,0.025796717f,-0.0585420504f,-0.0331749767f,-0.00471095974f,-0.0683756322f,0.0619786419f,0.150850549f,-0.301581264f,0.0158064812f,0.0668949336f,0.0240045916f,-0.0121377939f,-0.0347392075f,-0.281778365f,0.121754169f,0.161623657f,0.0747119412f,0.139027625f,-0.144426152f,-0.0458762087f,-0.0129371593f,0.165354684f,0.00936455932f,0.0615657084f,0.0655899942f,-0.0698695853f,-0.052994132f,-0.384851784f,0.423033476f,-0.00287530129f,0.0575512424f,0.0397480391f,-0.0773335323f,0.00306599913f,-0.0578767173f,0.316531688f,0.00381686469f,-0.0060816356f,-0.0572510138f,0.0680302829f,0.1290932f,0.0969372094f,0.072434552f,0.236802757f,-0.0659118593f,0.0711805671f,-0.168141514f,-0.322554111f,-0.0361107923f,0.0638245642f,-0.112720445f,0.00140461f,-0.0399631336f,0.0122548109f,-0.0649828017f,0.237167403f,-0.167791992f,0.0870348513f,-0.0231505856f,-0.0309811197f,0.107324749f,0.25214541f,0.0946144313f,0.222047403f,0.153789267f,-0.0693546757f,-0.0263586566f,-0.0953181162f,0.0188398901f,-0.15203464f,0.414190829f,0.134954765f,-0.0542947538f,0.197977975f,-0.221337691f,0.118480518f,0.0125316242f,-0.142037377f,-0.0913458467f,-0.136096552f,0.0568877421f,0.417987496f,0.0407048054f,-0.0751560032f,-0.410313874f,-0.113319822f,-0.221733287f,0.47624597f,0.141178414f,-0.140136346f,-0.245049059f,0.292699218f,-0.305115193f,0.0903308466f,-0.0118093565f,0.76328522f,0.184514642f,-0.0484723113f,0.0723377168f,1.71665299f,-0.0374227911f,0.147536471f,-0.0558664016f,-0.0986959189f,0.25838089f,-0.143692926f,-0.00979547761f,-0.160485029f,-0.0854567513f,-0.00417465437f,0.204687908f,0.124579318f,0.0585011952f,0.00546030235f,-0.129718095f,-0.0396024659f,-0.0837967545f,0.19510439f,0.0801027268f,0.140662163f,-0.00808852445f,0.0347922221f,0.0358003154f,0.0772451386f,0.0328729749f,0.104643665f,0.0317583457f,-0.0205351245f,-0.0812441111f,-0.0338215716f,0.0208996069f,0.106405452f,-0.051947508f,0.149971396f,-0.0925236791f,-0.0545696467f,0.0788888335f,-0.0617592447f,0.0180972479f,-0.0142765334f,-0.00158832129f,0.0243659001f,-0.0761000961f,-0.0533908308f,-0.00368251931f,0.11213474f,-0.140045375f,0.106066793f,-0.00798349176f,0.0631749108f,0.0650617182f,0.0739720613f,0.0196545683f,0.0413370803f,0.056672778f,0.0188898109f,-0.104946248f,-0.0279061981f,0.0579444505f,-0.0739645064f,0.0534403548f,0.167991027f,-0.0635298491f,-0.0496445931f,0.0449382551f,-0.0814556405f,0.00157886511f,0.134332553f,0.0995729268f,0.0103129772f,-0.181042001f,-0.0936421305f,-0.0654525086f,-0.127459317f,0.0204577874f,0.109991856f,0.10787309f,0.0385146886f,-0.126067474f,0.0858242959f,-0.0378334858f,0.0366239585f,0.142049953f,-0.00391974393f,-0.152396604f,-0.00817822758f,-0.0584693141f,-0.0364640914f,-0.0367087387f,0.0895321146f,-0.12534599f,-0.116982736f,-0.0758479312f,-0.0754894018f,-0.190499514f,0.0127668614f,-0.00923017878f,-0.181062624f,0.058132235f,-0.201547325f,-0.0188836381f,-0.207527891f,-0.103457421f,-0.177899688f,0.216855675f,0.174339056f,0.104755349f,-0.0576479733f,-0.0290995575f,0.143352762f,-0.672481179f,-0.00247217901f,-0.166387767f,-0.230813995f,-0.326928705f,0.431005001f,0.452418268f,0.044315882f,-0.169952974f,-0.0155172832f,-0.224393219f,-0.132626042f,0.183276743f,1.77384031f,1.57409775f,-0.0349120945f,0.250549942f,0.0549430624f,0.00283114798f,-0.187877029f,-0.122572869f,-0.566341281f,-0.000197903573f,-0.0379303098f,0.188057899f,-0.190812677f,0.0402397066f,0.0338991098f,-0.257802784f,-0.146706f,0.218979061f,-0.0763995647f,0.304563105f,0.00842405204f,-0.012383054f,-0.283085614f,-0.060138166f,-0.246339157f,0.0519502535f,-0.240539297f,-0.00558379991f,-0.0547632799f,-0.328151256f,0.0595499761f,0.024090698f,0.0281176362f,-0.0576687269f,0.0196743961f,-0.0498917997f,-0.334749371f,0.0615766644f,0.0644692481f,-0.0724426135f,-0.0371327363f,-0.0445202962f,-0.0800978467f,-0.360870868f,-0.0879896805f,-0.100133307f,0.0226694681f,-0.179598048f,0.0165100154f,0.0100137889f,-0.321165115f,-0.154303581f,-0.0303054657f,0.123027317f,0.0419304036f,0.0267785694f,0.00437118812f,-0.471519649f,-0.0174912736f,0.0870188102f,0.185009167f,-0.0397359952f,0.0245355871f,0.0711416528f,-0.144194722f,-0.227215409f,0.0719778314f,0.297168672f,0.0186616238f,0.106214419f,-0.0529407971f,0.0109083559f,0.0781363025f,0.0931331441f,-0.0403028913f,0.0426367521f,-0.146333948f,0.0371560752f,-0.0682456642f,0.083617337f,-0.00977722462f,0.121462025f,0.0223968569f,-0.117255747f,0.0811857209f,0.0106168659f,0.108088806f,-0.0768960491f,0.188291639f,0.0142577346f,-0.156552225f,0.024979122f,-0.197574526f,0.00741450163f,0.0406796634f,0.261568427f,0.0434952416f,0.0800376758f,-0.0490534268f,-0.273526579f,0.0241424199f,-0.00843974575f,0.181323722f,0.0245973915f,0.0169294793f,0.0814727917f,0.445720941f,-0.339772016f,-0.0188979637f,0.0873919502f,-0.308327466f,-0.0273219142f,0.115614451f,-0.0636446625f,0.0969016626f,0.268056303f,0.0128450729f,-0.110556029f,0.360813528f,0.0559064411f,0.86369133f,0.620045781f,0.178376451f,0.324239731f,-0.47790578f,-0.437089443f,-0.00936363917f,-0.00525895087f,1.07991576f,0.0553175136f,0.0623913966f,-0.0674815476f,0.244194567f,-0.0646860898f,-0.34766829f,-0.147807285f,0.193949699f,0.0941860005f,0.0233984627f,0.000417017029f,-0.0156424418f,-0.0374892242f,0.0175934751f,-0.0258376505f,0.0240526646f,-0.0604164265f,-0.0727160946f,0.0295395087f,0.00072490098f,-0.0359368213f,0.0639963076f,-0.110449031f,-0.0112600336f,0.000276670791f,0.13106744f,0.0547618605f,-0.00179873873f,-0.0536629111f,0.0709133074f,-0.205846533f,0.0821932852f,0.15522407f,-0.0236238856f,0.0582708977f,-0.182163179f,0.117734581f,-0.0582276694f,0.0253449995f,0.103213288f,-0.0331291221f,0.0132639986f,-0.0819543973f,-0.181771964f,-0.0719015002f,0.0883258879f,0.145442352f,-0.0702124983f,0.0241770353f,-0.0987636819f,0.085019283f,-0.00202475023f,0.0693240762f,-0.028132759f,-0.0563377552f,-0.0613158084f,-0.11072997f,0.108169824f,-0.0389615819f,0.0977183431f,0.0537550934f,0.0884603485f,0.0399285555f,-0.0137734143f,0.0513593443f,-0.152020931f,0.0194748752f,-0.0440518148f,-0.110151708f,-0.0457983464f,0.231514364f,-0.0933235213f,-0.203174844f,0.0133862458f,0.197542354f,0.133848026f,-0.0507652313f,0.137364894f,-0.0788789913f,0.086860545f,-0.112658799f,-0.164898217f,-0.221087828f,-0.0572910495f,-0.0389342532f,0.13594228f,0.0410949402f,-0.0925304666f,5.54560211e-05f,-0.0571030192f,-0.0145921409f,-0.0914999992f,0.0183026642f,-0.0318617076f,0.0534189045f,0.0247214455f,-0.0418337807f,0.0167064797f,0.142290294f,-0.207838133f,-0.0783327147f,0.0253079608f,0.0621974692f,-0.0535141528f,-0.00756912399f,0.374568105f,-0.28701517f,-0.0305818021f,0.327747852f,-0.00168561121f,0.075253658f,0.0876174718f,-0.022849828f,-0.0845449418f,-0.163142487f,-0.845993817f,0.261926144f,1.6800313f,-0.134506315f,1.97487473f,0.240430117f,-0.0705726296f,-0.0853186026f,0.0766866878f,0.0644806176f,0.0610907897f,-0.193726644f,0.357549667f,-0.14188239f,-0.0458206311f,0.0529013835f,0.0566885136f,0.101744182f,-1.08505666f,0.109687023f,-1.06992412f,-0.0154109495f,0.0594681278f,-0.00730636064f,-0.18904531f,-0.0339320712f,-0.0650426149f,-0.0361465625f,-0.235612884f,0.219713703f,-0.108750537f,0.149141282f,0.0466508009f,0.0101667317f,-0.530775964f,-0.0437736884f,-0.13484779f,0.0341960229f,-0.00360704819f,-0.10098093f,-0.00144973875f,-0.553592265f,0.058845304f,-0.132479802f,0.0836745128f,0.039960932f,-0.0450558476f,0.0630117804f,-0.242763355f,-0.0542775467f,-0.206665471f,-0.109650731f,0.00348328915f,-0.135351568f,-0.032466203f,-0.421713114f,-0.0206511505f,-0.142052084f,0.0958070755f,0.126485363f,-0.0697613508f,-0.0600452609f,-0.541957676f,0.147271439f,-0.0667842478f,-0.101638772f,0.234805062f,-0.0459220074f,-0.0240777172f,-0.318723768f,-0.0397153124f,-0.105501778f,-0.0104215322f,0.10898672f,-0.016267132f,0.0289257001f,-0.197332874f,0.0150373979f,0.0324165747f,0.12548548f,0.000894418161f,0.217961505f,-0.0552029684f,-0.0243269634f,0.00582707347f,-0.0498440824f,0.105114967f,0.0423662215f,0.0880732238f,-0.0589850359f,0.0146361645f,-0.10172312f,-0.0363762528f,0.0706245452f,0.144494846f,-0.00387530611f,0.0660554841f,-0.0890467688f,-0.138288811f,-0.111186355f,0.227235347f,-0.123864353f,0.0643058345f,0.0836614668f,-0.26985094f,-0.0257607978f,0.0128583144f,0.300177634f,0.175005361f,0.0883968696f,-0.0644279122f,-0.174393535f,0.0838810578f,0.0717849582f,0.168665826f,0.048678901f,0.156180143f,-0.0402608216f,-0.0265993811f,0.304048568f,-0.391030371f,0.256348163f,-0.394595832f,-0.0292793345f,0.187474608f,-0.0835203826f,-0.357724458f,0.617155671f,0.0596991964f,-0.72527647f,-0.594409049f,-0.6025846f,0.651376665f,0.861364782f,0.124656938f,0.0284874067f,0.0490187667f,0.174405351f,0.172000185f,0.108765751f,0.72301513f,0.750472307f,-0.0413213558f,-0.09366896f,-0.166981414f,-0.00381923234f,-0.112765618f,0.124499165f,-0.0847953036f,-0.0619802102f,0.466154426f,0.411592454f,0.259973139f,-0.00973374397f,0.59979713f,0.0181589667f,-0.0812386349f,0.220208466f,0.184072167f,0.242946044f,0.014990123f,0.168194592f,0.00545647275f,-0.0319720469f,0.166981816f,0.358600855f,-0.0299126133f,0.078759186f,-0.0424911268f,-0.0344931632f,-0.04472626f,0.0714468136f,0.0426343866f,0.182995915f,0.0104624927f,0.0735857859f,-0.0527026467f,-0.0201856531f,0.0799826309f,0.0336909182f,-0.0452908278f,0.0270262789f,-0.00471215602f,0.0512496717f,0.0784439966f,0.155674517f,0.19878304f,0.171480224f,-0.0442910753f,0.0372003168f,0.016056696f,-0.114838772f,0.160517678f,0.332235485f,-0.126087323f,0.085387446f,0.0254157949f,-0.0453545749f,0.0377795212f,0.2289242f,0.406630784f,0.0733532831f,0.0409698747f,-0.0404767171f,0.0655773059f,0.0166293755f,0.450566828f,0.252222627f,0.122164577f,-0.0231060665f,0.00916731171f,0.0415597185f,0.0313348249f,0.0248385072f,0.184380203f,-0.0861566439f,0.0821471289f,0.0379976928f,-0.0184692331f,-0.0528981723f,0.206494004f,0.277686417f,0.311230421f,0.078611806f,0.0304918885f,-0.0501532629f,-0.0144959223f,0.128659025f,0.035793744f,0.0728759989f,-0.017978834f,0.0165202543f,0.0402312241f,-0.0530207045f,0.29233399f,0.412943363f,0.0613102317f,0.0571993068f,0.0644972026f,-0.00742855528f,-0.0863385573f,0.00988808181f,0.190411851f,-0.238321304f,0.00831055548f,0.0830300823f,0.0313329101f,0.160491943f,0.103642568f,0.0393656939f,0.0318853036f,-0.0479443148f,0.0754479021f,0.0473199412f,-0.355376124f,0.18203637f,0.199962974f,0.33474642f,0.219496191f,-0.307447851f,-0.550556064f,0.118073143f,0.384766459f,0.25354284f,-0.0750874951f,-0.0245455895f,0.0935691893f,-0.00499611348f,-0.736298442f,0.695988417f,-0.00520041073f,0.00970365852f,-1.85302019f,0.176338181f,0.0225415099f,0.172405139f,0.108208448f,0.0207801796f,0.0414184742f,0.189851761f,-0.0441229865f,-0.00614836439f,-0.0219990723f,0.094310686f,-0.0510609932f,0.213317901f,0.0838571414f,-0.1837558f,-0.0244801976f,-0.028599469f,-0.0453882441f,0.0511644483f,0.217275381f,0.0150221307f,-0.083211191f,0.0189244505f,0.0738504827f,0.111891709f,0.0571464561f,0.0964486077f,-0.144006729f,0.0265416354f,0.0721219033f,0.0465284996f,-0.0290882364f,-0.0355946794f,0.0150088407f,-0.0265716575f,0.0411123261f,0.0393661447f,-0.0443820991f,0.0608922169f,0.0584953167f,0.210111648f,-0.0261128098f,-0.172512367f,0.0986017138f,-0.0522725806f,0.0564534366f,-0.0351420902f,0.286908925f,0.19462356f,-0.123788685f,0.302832782f,-0.0754346028f,0.0846122503f,0.00787155144f,-0.114642605f,-0.178649455f,0.109314732f,0.226932913f,0.0461709276f,-0.0810254142f,-0.0246636923f,0.246191233f,-0.0828173906f,-0.225116223f,0.169682711f,-0.0163678713f,0.0702480227f,-0.0085620787f,0.226321459f,0.024639627f,0.128153533f,0.419747502f,0.0576920845f,0.067885533f,-0.0133229848f,-0.28038016f,-0.318131447f,0.00347985816f,0.216490209f,0.0081113223f,0.0129325213f,0.0683118105f,-0.10411334f,-0.154852524f,0.045454707f,-0.0432439148f,-0.0217361376f,0.102166846f,-0.0318836831f,0.160275295f,-0.193165183f,0.0456159264f,0.128249809f,0.0243707858f,0.00193454616f,0.00413952488f,-0.0653627366f,-0.315953195f,0.202077106f,0.0124354726f,-0.0246465839f,0.016707873f,0.00994397886f,-0.0536077656f,0.0335107632f,0.0108736716f,0.0578777269f,-0.116130315f,-0.0119091449f,0.441843271f,-0.28665477f,0.17834875f,-0.467236549f,-0.312865704f,0.025070738f,0.507597446f,-0.115696311f,-0.0848477185f,-0.000595500285f,0.0550227724f,-0.291734129f,0.0373635255f,0.0647092089f,0.731938243f,-0.202254802f,0.138155162f,0.251250654f,1.67061913f,0.0207856894f,0.00964373071f,-0.030171603f,-0.0714610666f,0.144923136f,-0.0943852216f,-0.0126347104f,0.152598873f,0.0346642248f,-0.0293672327f,0.0343651623f,-0.083649762f,0.276546806f,0.140784159f,-0.220122367f,0.00510708289f,-0.0429874882f,-0.054536514f,0.0402334407f,0.184714153f,0.322660595f,-0.295262009f,-0.0421878956f,0.0726725385f,-0.0969801843f,-0.145512074f,-0.0918684527f,-0.0163456649f,0.231344804f,-0.037973579f,0.0516688265f,-0.0852395222f,0.143290043f,-0.0584788695f,0.073010616f,-0.106055953f,-0.0682111457f,0.0774045065f,0.0101916706f,-0.0389284231f,0.172393367f,0.210536599f,-0.218297914f,-0.195925131f,0.0492344983f,0.0061237691f,-0.084303543f,0.148139194f,0.220226228f,-0.118726775f,0.00464942353f,-0.0309203956f,-0.188970655f,-0.18499206f,-0.017220784f,0.0333973542f,-0.0205857325f,0.140630543f,0.0082639996f,-0.122552089f,-0.0857351646f,0.0926206708f,0.540146112f,-0.309642345f,-0.0351506509f,0.0619601794f,0.00759675214f,0.0963352174f,0.235891014f,0.172930807f,-0.144957438f,0.0630268008f,-0.0563292131f,0.0740352646f,-0.0503594093f,0.0503046662f,0.431557775f,-0.300371408f,-0.0998069122f,0.0844271705f,-0.155909643f,0.0720596239f,0.207973853f,0.315829217f,-0.264451414f,-0.148904368f,0.00517650926f,-0.0584083982f,0.0382966511f,0.0115724923f,0.282246441f,-0.195812523f,-0.0473260619f,-0.0370772593f,-0.21302174f,-0.0497435108f,0.182206482f,0.290899783f,-0.296596318f,-0.171824455f,-0.0817141235f,-0.418753326f,0.178661317f,0.15641363f,0.0598160699f,0.0701306909f,-0.108861215f,0.0982946157f,-0.0978607163f,0.141665205f,0.235131577f,0.486569792f,0.169586107f,0.435438126f,-0.333135486f,-0.471052408f,-0.22003825f,0.354121298f,0.554545641f,-0.0692680031f,-0.260185301f,-0.285332769f,-0.202737272f,-1.01550341f,-0.111778848f,-0.0454854257f,-0.0375084095f,-0.544037342f,-0.0974342674f,-0.124651417f,-0.0962837934f,-0.0284231398f,0.00918802805f,-0.0482679829f,0.583342969f,-0.0239441153f,0.0742096379f,0.0767072737f,-0.219355553f,0.153096646f,-0.179992616f,-0.12895824f,0.195370063f,0.0293677151f,0.0741437748f,0.0364777073f,0.0681138411f,0.115384147f,0.0237309784f,-0.167754516f,-0.0357588604f,0.0527359024f,-0.115525641f,0.0648058802f,0.163975284f,-0.0082964357f,-0.190331236f,0.0577787347f,-0.0113899568f,-0.224612802f,-0.0971952677f,0.249018118f,-0.00269796117f,0.00721060578f,-0.180699944f,0.0302725136f,0.0229151137f,0.120289236f,0.0855229571f,-0.0585376956f,-0.0982288644f,-0.0571796894f,0.054938104f,0.00354495645f,-0.0418494232f,0.126053989f,0.354729414f,-0.0203341711f,-0.0342701524f,0.0792526305f,0.019905664f,-0.0210621655f,0.102298684f,0.212654635f,-0.148225799f,-0.144260198f,-0.0772569701f,-0.329409957f,-0.0612423532f,-0.0375845656f,0.0924009681f,0.172578171f,0.194607019f,-0.0821518227f,-0.344680399f,0.114727512f,0.182190225f,-0.0288531724f,0.0462033413f,-0.0720323771f,0.0770756602f,-0.386196971f,-0.0209155586f,-0.0343061462f,0.0852336511f,0.132548168f,0.191390827f,0.0507356524f,-0.370538294f,0.0649135858f,0.226305693f,0.0411128215f,0.183074415f,0.198671162f,0.0865173042f,-0.504832983f,-0.0180853736f,0.187359139f,0.0555106141f,0.0622610115f,-0.00473381532f,0.0495403782f,-0.317898095f,-0.111156359f,0.112698838f,-0.00492443284f,0.0678390935f,-0.0275405664f,-0.0150996624f,-0.185189366f,0.201843277f,-0.154095277f,0.00676186522f,-0.393986046f,-0.0659040883f,0.211733058f,-0.0927380621f,-0.0943962634f,0.428293496f,-0.181472644f,0.382801771f,0.0974554121f,0.144378692f,-0.413082957f,-0.371459067f,0.141833112f,-0.0485245176f,0.0767451152f,-0.098191984f,-0.0587808602f,0.0923839509f,-0.241513863f,-0.0315309651f,-0.0465459675f,0.0711151958f,-0.379886478f,-0.0322136916f,-0.120483413f,-0.147311047f,0.166349426f,-0.0741572306f,-0.0469962433f,-0.0505873039f,0.183539495f,-0.0767240971f,0.0144411651f,0.95665127f,-0.0860293806f,-0.0730253085f,0.173624977f,-0.133655638f,-0.0528959744f,0.0254249685f,0.11443378f,0.0142519511f,-0.017865207f,0.139470875f,0.09560927f,0.107584998f,0.0484268405f,0.260843784f,-0.114217244f,0.120388091f,0.159550324f,0.183702841f,0.000754854234f,0.0655659884f,0.322841734f,-0.0924518183f,0.0302713141f,-0.0416750833f,0.016188696f,0.104298264f,0.0438976847f,0.163789362f,0.0365132391f,-0.0080578709f,0.218568906f,0.0135295931f,0.154412463f,0.0715520754f,0.284253359f,0.0193343125f,-0.028616555f,0.122095674f,0.0437517799f,-0.00276093045f,0.0531489179f,0.295518786f,0.012544197f,-0.0232091267f,0.0966181308f,0.0542741455f,-0.0277057905f,0.00890627503f,0.380860627f,0.00296541816f,-0.123924933f,0.185258478f,0.0642561987f,-0.0154038128f,-0.0354861952f,0.407020032f,0.0369998068f,-0.026910875f,-0.017110208f,0.0467380807f,0.0193983819f,-0.0122541394f,0.506492913f,-0.165759787f,0.0509006716f,0.0391983651f,0.0515742339f,0.0309692454f,0.0354079157f,0.285044104f,-0.0658851787f,-0.0336599499f,-0.00450741593f,0.049654413f,-0.178440437f,-0.0544989444f,0.399213403f,0.0335476287f,-0.0495009348f,0.0165476259f,0.0884623453f,0.0300312843f,0.038987007f,0.29196918f,-0.0156178446f,0.0626714006f,0.0436907634f,-0.195106849f,-0.09694767f,0.0619902536f,-0.0472285785f,0.152313456f,-0.152724564f,0.106535167f,0.651565433f,0.0232704524f,-0.323767751f,-0.0721177757f,-0.570885658f,0.36740604f,-0.0710605085f,-0.408829421f,0.395731956f,-0.161642224f,0.408718497f,0.720458448f,0.133950219f,-0.0188845918f,-0.0916487947f,0.0800363421f,0.0365790837f,0.0236934256f,0.573074877f,0.613619268f,0.0258492343f,-0.0187439099f,-0.178721368f,0.0159180127f,-0.457982391f,-0.0148181012f,-0.213040993f,-0.0274213757f,-0.0409486741f,0.178855762f,-0.163883686f,-0.0103731435f,-0.0599079765f,0.249149323f,-0.0998053551f,0.00278028613f,0.0498298518f,-0.122364581f,0.00237970147f,0.0162742846f,0.218688145f,0.0690677091f,0.0445009135f,0.794047594f,0.129680797f,-0.147411957f,0.0378645211f,0.00252392958f,-0.00392290344f,-0.0107215084f,0.200192541f,-0.0924987942f,0.0220641103f,0.0783076808f,-0.0439238064f,-0.0163290594f,0.0056722532f,0.259790748f,-0.304965049f,-0.0236145593f,-0.0747841299f,0.29905498f,-0.122299314f,0.133852988f,0.0383276977f,-0.25215593f,-0.155032799f,0.0876529515f,0.257764548f,0.0137788365f,0.107316002f,0.507619739f,-0.324761033f,-0.11493291f,0.0851259828f,0.113448374f,0.116118021f,0.12959902f,0.104001142f,-0.0835226998f,0.0596879721f,0.00575705012f,-0.198561847f,-0.0154058347f,-0.0148916757f,0.161972716f,-0.0384013578f,-0.0352647863f,0.0666239411f,-0.129589394f,0.0571908168f,0.00281918235f,0.250455469f,-0.595936358f,-0.01285493f,0.080005236f,-0.0233299974f,-0.0280426666f,0.132037163f,0.176698297f,0.198399961f,0.0247747079f,0.0660374239f,-0.0522323735f,0.0815841034f,0.115945585f,0.39946565f,-0.0776648298f,-0.0394173972f,-0.076481685f,0.377881974f,0.0484939665f,0.427544713f,0.319343239f,0.295224518f,-0.126895085f,-0.00699719135f,0.0183860548f,-0.0407709517f,0.181169108f,0.24134697f,-0.091438517f,-0.0203515347f,-0.0486463495f,-0.597617805f,0.235075384f,0.0276113804f,-0.0604921281f,-0.337773651f,0.165735498f,-0.0425634533f,-0.0647430345f,0.00802647974f,0.164243221f,0.499501437f,0.164246231f,0.433566004f,-0.428409964f,-0.462947965f,-0.167582408f,0.408909231f,0.554198205f,0.206169665f,-0.234768391f,-0.225423336f,-0.235842928f,-1.15373254f,-0.00780297862f,-0.114679836f,-0.0849463269f,-0.859830916f,-0.146029323f,-0.276932955f,-0.172741726f,0.306831658f,0.053898111f,0.18333219f,-0.000988545362f,-0.0796151236f,-0.0531564951f,0.0710007548f,0.37595734f,0.0581752956f,0.190137669f,-0.0150673017f,0.297919363f,0.0209042523f,0.186989129f,0.0946993828f,0.00886239111f,-0.00216494012f,0.22824499f,-0.0467651561f,-0.0117578544f,0.00753263524f,0.182685748f,-0.0460784622f,0.0620839335f,0.122713856f,-0.0221343674f,-0.0352524072f,-0.0232785754f,0.324418664f,0.0983450711f,0.0148433931f,-0.095115073f,-0.0512171052f,0.0555446073f,0.0217198692f,0.160867497f,-0.0435852185f,0.0702271536f,0.0564169958f,0.0543588065f,0.0985601768f,-0.0198198147f,0.280412585f,0.151882768f,0.0110050542f,0.0571279675f,-0.0247706026f,-0.112143293f,0.0346094966f,0.277409762f,0.102570996f,-0.0876658633f,0.0199245606f,-0.0029912733f,0.0470084921f,-0.00601297803f,0.144117922f,-0.136632651f,-0.182326794f,-0.0619548149f,0.256233066f,-0.0244037248f,-0.0493073501f,0.429835975f,0.215702295f,-0.139129177f,-0.0732543468f,0.572699368f,-0.0761107057f,0.0131904632f,0.438415706f,0.145904213f,0.0411743075f,-0.0102014951f,0.271955043f,-0.0250497907f,-0.0667249113f,0.41775912f,0.0101905568f,0.000254104787f,-0.0328579731f,0.537635148f,0.0180332549f,0.0175817329f,0.405038059f,-0.15837723f,0.0729575306f,0.0429242924f,0.393835694f,0.0186747294f,0.000346692832f,0.24457632f,-0.0987747386f,0.0371542163f,0.0262748636f,0.458652109f,-0.164193064f,0.0412776545f,0.139774054f,0.311940104f,-0.329687715f,-0.25389421f,0.657361329f,0.0637728423f,-0.288078517f,0.0224209595f,-0.476785332f,0.400968492f,0.0528511368f,-0.732208073f,-1.37350953f,-0.28729412f,0.563283384f,-0.453803927f,0.0392738171f,0.0156250168f,0.16512081f,0.226335272f,0.299602777f,0.352049172f,0.178354621f,-0.278785795f,-0.0299273711f,-0.0746754855f,-0.0286014657f,0.14574115f,0.610133588f,-0.234649926f};
static const float nn_b[]={0.0225237906f,0.09481778f,0.0592294931f,0.0796418563f,0.154493213f,-0.078883417f,-0.109911792f,0.19662638f,-0.0238810927f,0.119266726f,0.0112841865f,0.0560169667f,-0.118495539f,-0.0297248922f,-0.0576956421f,-0.133061215f,0.139361963f,-0.0994091853f,0.0520705022f,0.127208009f,0.0457938649f,-0.064016968f,-0.0682978407f,0.0096919788f,0.108334877f,0.297093332f,0.0422238968f,-0.190761283f,0.0375581421f,0.0943660885f,-0.00988123659f,0.0428336896f};
static const float nn_o[]={0.546735406f,0.920694292f,1.30221069f,-0.691221058f,0.680960178f,-0.675232649f,0.937945604f,-0.797594726f,1.01686645f,-0.719343424f,-0.827204823f,0.637137592f,0.755962312f,0.655161321f,-0.902280509f,0.616966426f,1.48373044f,0.735408306f,0.688295186f,-0.726015747f,-0.847705126f,-1.24816656f,-0.987532973f,-1.87813675f,-0.855505049f,0.650893986f,-0.932688355f,1.08760309f,0.80140847f,-0.969527125f,1.27014947f,-0.860699117f};
static const float nn_ob[]={0.311679691f};

static float nn_score(const float *f){
 float out=nn_ob[0];for(int h=0;h<32;h++){float v=nn_b[h];for(int j=0;j<128;j++)v+=nn_w[h*128+j]*f[j];if(v>0)out+=nn_o[h]*v;}return out;
}
static int diag_nn=1;
static const State *nn_root;
static Action nn_actions[ACTIONS];static int nn_ranks[ACTIONS],nn_count;
static void nn_begin(const State *s){
 nn_root=0;nn_count=0;if(!diag_nn)return;
 nn_count=generate(s,nn_actions);float scores[ACTIONS];
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

static Tracker tr;static int first;
static State obs;
static unsigned char stt[sizeof tt],sqtt[sizeof qtt],sev[sizeof eval_tt],sh[sizeof history];
void save_context(void){memcpy(stt,tt,sizeof tt);memcpy(sqtt,qtt,sizeof qtt);memcpy(sev,eval_tt,sizeof eval_tt);memcpy(sh,history,sizeof history);}
void restore_context(void){memcpy(tt,stt,sizeof tt);memcpy(qtt,sqtt,sizeof qtt);memcpy(eval_tt,sev,sizeof eval_tt);memcpy(history,sh,sizeof history);}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tr,0,sizeof tr);first=1;}
int observe(int *p){memset(&obs,0,sizeof obs);for(int i=0;i<UNITS;i++)if(p[4*i+1])obs.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};obs.side=p[56];obs.remaining=p[57];track_observation(&tr,&obs,first);first=0;save_context();return tr.n;}
void actual(int *a){track_action(&tr,*(Action*)a);}
int probe(int nn,int ms,int depth,int fut,int threat,int *a,uint64_t *m){
restore_context();diag_nn=nn;use_futility=fut;use_threat_q=threat;
/* Caches contain bounds from a different selective search when ablating. */
if(!fut||!threat){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);}
State s=obs;prepare(&s);uint64_t start=now_ns();turn_deadline=start+(uint64_t)ms*1000000;deadline=turn_deadline;
int d=depth,v;Action best={0};nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;
if(depth){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);nn_begin(&s);v=search(&s,depth,-INF,INF,&best);nn_root=0;}else best=choose(&s,&d,&v);
memcpy(a,&best,sizeof best);m[0]=d;m[1]=nodes;m[2]=qnodes;m[3]=now_ns()-start;m[4]=stopped;return v;}

int rng_matches(int *p){return tr.n!=1 || (obs.rng==(uint32_t)p[58] && obs.neutral_mask==p[59]);}
int label(int *p,int *a,int depth,int *valid){
State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];
memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(eval_tt,0,sizeof eval_tt);memset(history,0,sizeof history);diag_nn=0;nn_root=0;use_futility=0;use_threat_q=1;
prepare(&s);State child=apply(&s,*(Action*)a);deadline=now_ns()+2000000000ULL;turn_deadline=deadline;stopped=0;nodes=qnodes=0;Action best;int v=search(&child,depth,-INF,INF,&best);*valid=!stopped;return -v;}
