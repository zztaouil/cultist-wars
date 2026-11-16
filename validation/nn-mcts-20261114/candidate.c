#define _POSIX_C_SOURCE 200809L
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
#define BUDGET_MS 48
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
typedef struct { uint64_t key; int value; uint8_t depth, flag; uint16_t move; } QEntry;
static QEntry qtt[TT_SIZE];
typedef struct { uint64_t key; int value, valid; } EvalEntry;
static EvalEntry eval_tt[TT_SIZE];
static uint64_t deadline, turn_deadline, nodes, cutoffs, hits;
static int stopped;
static int use_futility = 1;
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
#ifndef LEADER_VALUE
#define LEADER_VALUE 0
#endif
#ifndef SAFETY_WEIGHT
#define SAFETY_WEIGHT 1
#endif
#ifndef RACE_WEIGHT
#define RACE_WEIGHT 60
#endif
#ifndef DEFENSIVE_PRUNING
#define DEFENSIVE_PRUNING 1
#endif
#ifndef CYCLE_Q
#define CYCLE_Q 1
#endif
static void incoming_damage(const State *s,int damage[UNITS]) {
memset(damage,0,sizeof(int)*UNITS);
unsigned shooters=(s->owned[0]|s->owned[1])&~s->leaders;
unsigned live=s->owned[0]|s->owned[1]|s->owned[2];
while(shooters) {
int i=__builtin_ctz(shooters);shooters&=shooters-1;
unsigned targets=live&~(1u<<i);
while(targets) {
int j=__builtin_ctz(targets);targets&=targets-1;
if(taxi[s->u[i].pos][s->u[j].pos]>6)continue;
int k=fast_hit(s,s->u[i].pos,s->u[j].pos);
if(k<0 || s->u[k].owner==s->u[i].owner)continue;
int d=7-taxi[s->u[i].pos][s->u[k].pos];
if(d>damage[k])damage[k]=d;
}
}
}
static int safety_value(const State *s) {
int damage[UNITS],value=0;incoming_damage(s,damage);
for(int i=0;i<UNITS;i++)if(s->u[i].hp && s->u[i].owner<2 && damage[i]) {
int d=damage[i],hp=s->u[i].hp;
int penalty=12*d+(2*d>=hp?90:0);
value+=(s->u[i].owner?1:-1)*penalty;
}
return value;
}
static int exposed_side(const State *s) {
int d[UNITS];incoming_damage(s,d);
unsigned own=s->owned[s->side];
while(own){int i=__builtin_ctz(own);own&=own-1;if(d[i])return 1;}
return 0;
}
static int conversion_contest(const State *s) {
int a=-1,b=-1;
for(int i=0;i<UNITS;i++)if(s->u[i].hp && s->u[i].leader){if(s->u[i].owner==0)a=i;else if(s->u[i].owner==1)b=i;}
if(a<0 || b<0)return 0;
for(int i=0;i<UNITS;i++)if(s->u[i].hp && !s->u[i].leader && s->u[i].owner<2
&& taxi[s->u[a].pos][s->u[i].pos]==1 && taxi[s->u[b].pos][s->u[i].pos]==1)return 1;
return 0;
}
#ifndef RECRUIT_WEIGHT
#define RECRUIT_WEIGHT 120
#endif
static int recruit_distance(const State *s, int leader, int target) {
if (leader < 0) return CELLS;
int p=s->u[leader].pos, q=s->u[target].pos;
if (taxi[p][q]==1) return 0;
int best=CELLS;
for(int k=0;k<4;k++) {
int n=adjacent[p][k];
if(n<0 || wall[n] || bit_has(s->occupied,n)) continue;
int d=distance_table[n][q];
if(d<best) best=d;
}
return best;
}
static int recruit_control(const State *s) {
int leader[2]={-1,-1},value=0;
unsigned leaders=s->leaders;
while(leaders) {int i=__builtin_ctz(leaders);leaders&=leaders-1;
if(s->u[i].owner<2) leader[s->u[i].owner]=i;}
unsigned ns=s->owned[2];
while(ns) {
int i=__builtin_ctz(ns);ns&=ns-1;
int d[2]={recruit_distance(s,leader[0],i),recruit_distance(s,leader[1],i)};
int access[2]={0,0};
for(int p=0;p<2;p++) {
if(d[p]>=CELLS) continue;
int risk=0;
unsigned enemies=s->owned[1-p]&~s->leaders;
while(enemies) {int j=__builtin_ctz(enemies);enemies&=enemies-1;
int damage=7-taxi[s->u[j].pos][s->u[i].pos];
if(damage>risk && fast_hit(s,s->u[j].pos,s->u[i].pos)==i) risk=damage;}
if(risk>s->u[i].hp) risk=s->u[i].hp;
access[p]=RECRUIT_WEIGHT*(2*s->u[i].hp-risk)/(2*s->u[i].hp*(d[p]+2));
}
value+=access[0]-access[1];
if(RACE_WEIGHT && d[0]!=d[1]) {
int winner=d[0]<d[1]?0:1;
if(d[winner]<CELLS)value+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);
}
}
return value;
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
score[side] += 1000 + 18 * u->hp + (u->leader ? LEADER_VALUE : 0);
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
if(SAFETY_WEIGHT)value+=SAFETY_WEIGHT*safety_value(s);
if(RECRUIT_WEIGHT && s->owned[2]) value += recruit_control(s);
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
static void move_neutral(State *s, int corpse) {
if (!s->rng_known || !s->neutral_mask) return;
unsigned index = random_next(s, 12);
unsigned mask = s->neutral_mask;
while (index-- && mask) mask &= mask - 1;
int id = mask ? __builtin_ctz(mask) : -1;
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
static uint16_t qmove(Action a) {
return (uint16_t)(1 + a.kind + 4 * a.unit + 64 * a.target);
}
static int pv_ab_value(const State *s);
#ifndef AB_NEURAL_VALUE
#define AB_NEURAL_VALUE 0
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
uint16_t hint = entry->key == s->key ? entry->move : 0;
if (entry->key == s->key && entry->depth == left + 1) {
++hits;
if (entry->flag == 0) return entry->value;
if (entry->flag == 1 && entry->value > alpha) alpha = entry->value;
if (entry->flag == 2 && entry->value < beta) beta = entry->value;
if (alpha >= beta) return entry->value;
}
int unstable = use_threat_q && left && (s->remaining <= left || material_threat(s));
int best = unstable ? -INF : (AB_NEURAL_VALUE ? pv_ab_value(s) : evaluate_live(s));
if (!left || (!unstable && best >= beta)) {
*entry = (QEntry){s->key,best,left+1,!left ? 0 : 1,0};
return best;
}
if (best > alpha) alpha = best;
Action actions[ACTIONS]; int n = generate_mode(s, actions, !unstable);
uint16_t best_move = 0;
for (int i = 0; i < n; ++i) {
Action a = actions[i];
actions[i].order *= 4096;
if (use_history && a.kind == MOVE)
actions[i].order = history[s->side][s->u[a.unit].leader][s->u[a.unit].pos][a.target];
if (qmove(a) == hint) actions[i].order += 100000000;
}
for (int i = 0; i < n; ++i) {
int k = i;
for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
Action tmp = actions[i]; actions[i] = actions[k]; actions[k] = tmp;
State next = apply(s, actions[i]);
value = -quiescence(&next, -beta, -alpha, left - 1);
if (stopped) return 0;
if (value > best) { best = value; best_move = qmove(actions[i]); }
if (value > alpha) alpha = value;
if (alpha >= beta) {
++cutoffs;
if (use_history && actions[i].kind == MOVE) {
int bonus = left * left * 16;
update_history(s, actions[i], bonus);
for (int j=0;j<i;j++) if (actions[j].kind == MOVE)
update_history(s, actions[j], -bonus);
}
break;
}
}
*entry = (QEntry){s->key,best,left+1,best <= original_alpha ? 2 : best >= original_beta ? 1 : 0,best_move};
return best;
}
#define NF 128
#ifndef NN_ENABLE
#define NN_ENABLE 1
#endif
static int victim(const State*s,Action a){return a.kind==SHOOT?fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos):-1;}
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
#define PV_WEIGHTS
/* Shared state encoder and small legal-action head. Feature version 1. */
#include <math.h>
#define PV_S 208
#define PV_A 32
#define PV_H 64
#define PV_Z 32
#define PV_N 16578
#ifdef PV_WEIGHTS
static float pv_w[PV_N];
static const char pv_blob[]=
"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"
;
static void pv_init(void){static int ready;if(ready)return;ready=1;
unsigned acc=0;int bits=0,n=0;unsigned char raw[PV_N*2];
for(int i=0;pv_blob[i]&&pv_blob[i]!='=';i++){unsigned char c=pv_blob[i];unsigned v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:63;acc=(acc<<6)|v;bits+=6;if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}}
for(int i=0;i<PV_N;i++){unsigned h=raw[2*i]|((unsigned)raw[2*i+1]<<8),e=(h>>10)&31,m=h&1023;
if(!e)pv_w[i]=(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);
else {unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(pv_w+i,&u,4);}}
}

#else
static float pv_w[PV_N];
static void pv_init(void) {}
#endif
static void pv_state(const State *s, float *x) {
    memset(x, 0, PV_S*sizeof(float));
    for (int i=0;i<CELLS;i++) x[i]=(float)wall[i];
    for (int i=0;i<UNITS;i++) {
        Unit u=s->u[i]; int k=91+8*i;
        x[k+7]=(s->neutral_mask>>i)&1;
        if (!u.hp) continue;
        x[k]=(u.pos%W)/12.f; x[k+1]=(u.pos/W)/6.f;
        x[k+2]=u.hp/10.f; x[k+3]=u.owner==s->side;
        x[k+4]=u.owner==1-s->side; x[k+5]=u.owner==2; x[k+6]=u.leader;
        if(u.owner==s->side)x[206]+=1.f/14;
        if(u.owner==1-s->side)x[207]+=1.f/14;
    }
    x[203]=s->remaining/150.f; x[204]=s->side; x[205]=s->rng_known;
}
static void pv_action(const State *s, Action a, float *x) {
    float old[NF]; features(s,a,old);
    memcpy(x,old+98,30*sizeof(float));
    x[30]=a.kind ? a.unit/13.f : 0;
    x[31]=a.kind==MOVE ? a.target/90.f : a.kind ? a.target/13.f : 0;
}
/* Flatten order: trunk0 W,b; trunk1 W,b; action W,b; policy W,b; value W,b. */
static float pv_encode(const State *s, float *z) {
    float x[PV_S],h[PV_H]; pv_init(); pv_state(s,x);
    int off=0;
    for(int i=0;i<PV_H;i++) {
        float v=pv_w[PV_S*PV_H+i];
        for(int j=0;j<PV_S;j++)v+=pv_w[i*PV_S+j]*x[j];
        h[i]=fmaxf(0,v);
    }
    off=PV_S*PV_H+PV_H;
    for(int i=0;i<PV_Z;i++) {
        float v=pv_w[off+PV_H*PV_Z+i];
        for(int j=0;j<PV_H;j++)v+=pv_w[off+i*PV_H+j]*h[j];
        z[i]=fmaxf(0,v);
    }
    off+=PV_H*PV_Z+PV_Z+PV_A*PV_Z+PV_Z+PV_Z+1;
    float v=pv_w[off+PV_Z];
    for(int i=0;i<PV_Z;i++)v+=pv_w[off+i]*z[i];
    return tanhf(v);
}
static float pv_logit(const State *s, Action a, const float *z) {
    float x[PV_A]; pv_action(s,a,x);
    int off=PV_S*PV_H+PV_H+PV_H*PV_Z+PV_Z;
    int poff=off+PV_A*PV_Z+PV_Z;
    float out=pv_w[poff+PV_Z];
    for(int i=0;i<PV_Z;i++) {
        float v=z[i]+pv_w[off+PV_A*PV_Z+i];
        for(int j=0;j<PV_A;j++)v+=pv_w[off+i*PV_A+j]*x[j];
        out+=pv_w[poff+i]*fmaxf(0,v);
    }
    return out;
}

static const State *nn_root;
static Action nn_actions[ACTIONS];static float nn_logits[ACTIONS];static int nn_count;
static void nn_begin(const State *s){nn_root=0;nn_count=0;if(!NN_ENABLE)return;
float z[PV_Z];pv_encode(s,z);nn_count=generate(s,nn_actions);
for(int i=0;i<nn_count;i++){if(now_ns()>=deadline){nn_count=0;return;}nn_logits[i]=pv_logit(s,nn_actions[i],z);}nn_root=s;}
static int nn_order(Action a){for(int i=0;i<nn_count;i++)if(same(a,nn_actions[i]))return (int)(1000*nn_logits[i]);return 0;}
#ifndef AB_NEURAL_VALUE
#define AB_NEURAL_VALUE 0
#endif
static int pv_ab_value(const State *s){float z[PV_Z];return (int)(10000*pv_encode(s,z));}
static int search(const State *s, int depth, int alpha, int beta, Action *chosen) {
if (!depth) return quiescence(s, alpha, beta, 4 + (CYCLE_Q && conversion_contest(s) ? 2 : 0));
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
int selective = !AB_NEURAL_VALUE && use_futility && depth == 1 && beta == alpha + 1
&& s->remaining > 6 && abs(alpha) < WIN / 2
&& (!DEFENSIVE_PRUNING || (!material_threat(s) && !exposed_side(s)));
int static_value = selective ? evaluate_live(s) : 0;
Action quiets[UNITS * 4]; int quiet_count = 0;
for (int i = 0; i < n; ++i) {
int k = i;
for (int j = i + 1; j < n; ++j) if (actions[j].order > actions[k].order) k = j;
Action swap = actions[i]; actions[i] = actions[k]; actions[k] = swap;
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
static Action ab_choose(const State *s, int *completed_depth, int *advantage) {
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
#include <math.h>
#ifndef MCTS_NODES
#define MCTS_NODES 8192
#endif
#ifndef MCTS_EDGES
#define MCTS_EDGES 262144
#endif
#ifndef MCTS_SIMS
#define MCTS_SIMS 1000000
#endif
#ifndef MCTS_C
#define MCTS_C 1.5f
#endif
#ifndef MCTS_VALUE
#define MCTS_VALUE 1
#endif
#ifndef MCTS_SEARCH
#define MCTS_SEARCH 1
#endif
#ifndef MCTS_UNIFORM
#define MCTS_UNIFORM 0
#endif
#ifndef MCTS_CLOCK
#define MCTS_CLOCK 1
#endif
typedef struct { int first,count,visits; float value; } MCNode;
typedef struct { Action a; int child,visits; float prior,sum; } MCEdge;
static MCNode mc_nodes[MCTS_NODES];
static MCEdge mc_edges[MCTS_EDGES];
static int mc_n,mc_e,mc_calls,mc_simulations,mc_max_depth,mc_full;
static uint64_t mc_deadline;
static int mc_expired(void) { return MCTS_CLOCK && now_ns()>=mc_deadline; }
static float mc_terminal_value(int value) {return value>0?1.f:value<0?-1.f:0.f;}
static float mc_heuristic(const State *s) {return tanhf(evaluate(s)/2000.f);}
/* An incomplete expansion never enters the tree. */
static int mc_expand(const State *s) {
    if(mc_n==MCTS_NODES){mc_full=1;return -1;}
    int terminal_value;
    if(terminal(s,&terminal_value)) {
        int id=mc_n++;mc_nodes[id]=(MCNode){0,0,0,mc_terminal_value(terminal_value)};return id;
    }
    Action actions[ACTIONS];int n=generate(s,actions);
    if(mc_e+n>MCTS_EDGES){mc_full=1;return -1;}
    float priors[ACTIONS],maxlog=-1e30f,z[PV_Z];
    float value=pv_encode(s,z);mc_calls++;
    if(!MCTS_VALUE)value=mc_heuristic(s);
    for(int i=0;i<n;i++) {
        if(mc_expired())return -1;
        priors[i]=MCTS_UNIFORM?0:pv_logit(s,actions[i],z);
        if(priors[i]>maxlog)maxlog=priors[i];
    }
    float total=0;
    for(int i=0;i<n;i++){priors[i]=expf(priors[i]-maxlog);total+=priors[i];}
    if(mc_expired())return -1;
    int id=mc_n++;mc_nodes[id]=(MCNode){mc_e,n,0,value};
    for(int i=0;i<n;i++)mc_edges[mc_e++]=(MCEdge){actions[i],-1,0,priors[i]/total,0};
    return id;
}
static void mc_backup(const int *path, const int *parents, int length, int leaf, float value) {
    mc_nodes[leaf].visits++;
    for(int i=length-1;i>=0;i--){
        value=-value;MCEdge *edge=&mc_edges[path[i]];
        edge->visits++;edge->sum+=value;mc_nodes[parents[i]].visits++;
    }
}
static Action choose(const State *input,int *completed_depth,int *advantage) {
    if(!MCTS_SEARCH)return ab_choose(input,completed_depth,advantage);
    uint64_t start=now_ns();
    mc_deadline=turn_deadline?turn_deadline:start+(uint64_t)BUDGET_MS*1000000ULL;
    nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;
    mc_n=mc_e=mc_calls=mc_simulations=mc_max_depth=mc_full=0;
    State root=*input;prepare(&root);
    Action legal[ACTIONS];int count=generate(&root,legal);
    Action best=count?legal[0]:(Action){WAIT,0,0,0};
    *completed_depth=0;*advantage=0;
    int r=mc_expand(&root);if(r<0)return best;
    for(int sim=0;sim<MCTS_SIMS&&!mc_full&&!mc_expired();sim++) {
        State s=root;int id=0,len=0,path[151],parents[151],ok=1;
        while(mc_nodes[id].count) {
            if(mc_expired()||len>=150){ok=0;break;}
            MCNode *n=&mc_nodes[id];int selected=n->first;
            float best_score=-1e30f,explore=MCTS_C*sqrtf((float)(n->visits+1));
            for(int j=n->first;j<n->first+n->count;j++) {
                MCEdge *e=&mc_edges[j];
                float q=e->visits?e->sum/e->visits:n->value;
                float score=q+explore*e->prior/(1+e->visits);
                if(score>best_score){best_score=score;selected=j;}
            }
            path[len]=selected;parents[len++]=id;
            MCEdge *edge=&mc_edges[selected];s=apply(&s,edge->a);
            if(edge->child<0){
                int child=mc_expand(&s);if(child<0){ok=0;break;}
                edge->child=child;id=child;break;
            }
            id=edge->child;
        }
        if(!ok)break;
        mc_backup(path,parents,len,id,mc_nodes[id].value);
        mc_simulations++;if(len>mc_max_depth)mc_max_depth=len;
        if(!mc_nodes[0].count)break;
    }
    if(mc_nodes[0].count){
        int selected=mc_nodes[0].first;
        for(int j=selected+1;j<mc_nodes[0].first+mc_nodes[0].count;j++) {
            MCEdge *a=&mc_edges[j],*b=&mc_edges[selected];
            if(a->visits>b->visits||(a->visits==b->visits&&a->prior>b->prior))selected=j;
        }
        MCEdge *e=&mc_edges[selected];best=e->a;
        *advantage=(int)(10000*(e->visits?e->sum/e->visits:mc_nodes[0].value));
    }else *advantage=(int)(10000*mc_nodes[0].value);
    nodes=mc_simulations;*completed_depth=mc_max_depth;
    return best;
}

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
