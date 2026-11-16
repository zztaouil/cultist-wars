// Primary standalone C++17 engine.
#define _POSIX_C_SOURCE 200809L
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
constexpr int W = 13;
constexpr int H = 7;
constexpr int CELLS = (W * H);
constexpr int UNITS = 14;
constexpr int ACTIONS = (1 + UNITS * (4 + UNITS));
constexpr int INF = 10000000;
constexpr int WIN = 1000000;
constexpr int TT_SIZE = 65536;
#ifndef BUDGET_MS
#define BUDGET_MS 49
#endif
struct Unit { uint8_t pos, hp, owner, leader; };
struct Bits { uint64_t lo, hi; };
struct State {
Unit u[UNITS];
uint32_t rng;
uint16_t neutral_mask;
uint8_t side, rng_known;
int remaining, ready;
uint64_t key, metadata;
Bits occupied;
uint16_t owned[3], leaders;
uint64_t occupants[6];
};
enum { WAIT, MOVE, SHOOT, CONVERT };
struct Action { int kind, unit, target, order; };
struct Entry { uint64_t key; int depth, value, flag; Action best; };
struct Ray { unsigned char length, cell[CELLS]; };
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
s->occupied = Bits{0,0}; s->leaders = 0;
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
struct QEntry { uint64_t key; int value; uint8_t depth, flag; uint16_t move; };
static QEntry qtt[TT_SIZE];
struct EvalEntry { uint64_t key; int value, valid; };
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
ray_bits[a][b] = Bits{0,0}; taxi[a][b] = manhattan(a,b);
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
while (!bit_has(Bits{lo,hi},path->cell[k])) ++k;
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
#ifndef LETHAL_PENALTY
#define LETHAL_PENALTY 180
#endif
#ifndef LEADER_EXPOSURE
#define LEADER_EXPOSURE 18
#endif
#ifndef RECAPTURE_DISCOUNT
#define RECAPTURE_DISCOUNT 1
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
int penalty=12*d+(2*d>=hp?90:0)+(d>=hp?LETHAL_PENALTY:0);
// Exposed leaders also risk losing the remaining recruitment economy.
if(s->u[i].leader) {
int neutrals=__builtin_popcount((unsigned)s->owned[2]);
penalty+=LEADER_EXPOSURE*d*(neutrals<3?neutrals:3);
}
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
static int recruit_control2(const State *s,int *race_out);
static int recruit_control(const State *s){int r;int a=recruit_control2(s,&r);return a+r;}
static int recruit_control2(const State *s,int *race_out) {
int leader[2]={-1,-1},value=0,race=0;
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
// A neutral beside the enemy leader cannot be counted as secure material.
if(RECAPTURE_DISCOUNT && d[1-p]==0) access[p]/=2;
}
value+=access[0]-access[1];
if(RACE_WEIGHT && d[0]!=d[1]) {
int winner=d[0]<d[1]?0:1;
if(d[winner]<CELLS)race+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);
}
}
*race_out=race;return value;
}
static const long long TEXEL_W[3][8]={{1000000,41727,6979,2495,126,2417,7878,7612},{1000000,46140,195,2940,1307,2949,6883,2690},{1000000,16670,0,0,1338,2872,0,0}};
static int evaluate_live(const State *s) {
uint64_t key = s->key ^ s->metadata;
EvalEntry *entry = &eval_tt[key & (TT_SIZE - 1)];
if (entry->valid && entry->key == key) return s->side ? -entry->value : entry->value;
long long t[8]={0,0,0,0,0,0,0,0};
for (int side = 0; side < 2; ++side) {
int sg=side?-1:1;unsigned own = s->owned[side];
while (own) {
int i = __builtin_ctz(own); own &= own - 1;
const Unit *u = &s->u[i]; int nearest = CELLS;
t[0]+=sg;t[1]+=sg*u->hp;
unsigned targets = s->owned[1-side];
if (u->leader) targets = (targets | s->owned[2]) & ~s->leaders;
while (targets) {
int j = __builtin_ctz(targets); targets &= targets - 1;
int p = s->u[j].pos, dist = distance_table[u->pos][p];
if (dist < nearest) nearest = dist;
if (u->leader) t[2]+=sg*proximity[u->pos][p];
}
if (nearest < CELLS) { if(u->leader) t[3]+=sg*leader_bonus[nearest]; else t[4]+=sg*fighter_bonus[nearest]; }
}
}
if(SAFETY_WEIGHT)t[5]=SAFETY_WEIGHT*safety_value(s);
if(RECRUIT_WEIGHT && s->owned[2]){int r;t[6]=recruit_control2(s,&r);t[7]=r;}
int nn_=__builtin_popcount((unsigned)s->owned[2]),nl_=0;{unsigned ls=s->leaders;while(ls){int i=__builtin_ctz(ls);ls&=ls-1;if(s->u[i].owner<2)nl_++;}}
int rg=nn_==0?2:(nl_==2?0:1);long long acc=0;for(int k=0;k<8;k++)acc+=TEXEL_W[rg][k]*t[k];
int value=(int)(acc/1000);
*entry = EvalEntry{key,value,1};
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
if (!tactical) actions[n++] = Action{WAIT, 0, 0, -100};
unsigned own = s->owned[s->side];
unsigned live = s->owned[0] | s->owned[1] | s->owned[2];
while (own) {
int i = __builtin_ctz(own); own &= own - 1;
const Unit *u = &s->u[i];
for (int d = 0; !tactical && d < 4; ++d) {
int p = adjacent[u->pos][d];
if (p >= 0 && !wall[p] && !bit_has(s->occupied,p))
actions[n++] = Action{MOVE, i, p, 0};
}
unsigned seen = 0;
unsigned targets = u->leader ? (s->owned[1-s->side] | s->owned[2]) & ~s->leaders : live & ~(1u << i);
while (targets) {
int j = __builtin_ctz(targets); targets &= targets - 1;
const Unit *v = &s->u[j];
int dist = taxi[u->pos][v->pos];
if (u->leader) {
if (!v->leader && v->owner != s->side && dist == 1)
actions[n++] = Action{CONVERT, i, j, v->owner == 2 ? 2000 : 4000};
} else if (dist <= 6) {
int hit = fast_hit(s, u->pos, v->pos);
if (hit < 0 || (seen & (1u << hit))) continue;
seen |= 1u << hit;
const Unit *victim = &s->u[hit];
int damage = 7 - taxi[u->pos][victim->pos];
int order = damage * 40 + (damage >= victim->hp ? 1500 : 0);
if (victim->owner == s->side) order = -order;
if (victim->owner == 2) order /= 4;
if (!tactical || order > 0) actions[n++] = Action{SHOOT, i, j, order};
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
if (!s->ready) { occupied = Bits{0,0};
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
if ((nodes & 7u) == 0 && now_ns() >= deadline) stopped = 1;
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
int best = unstable ? -INF : evaluate_live(s);
if (!left || (!unstable && best >= beta)) {
*entry = QEntry{s->key,best,static_cast<uint8_t>(left+1),static_cast<uint8_t>(!left ? 0 : 1),0};
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
*entry = QEntry{s->key,best,static_cast<uint8_t>(left+1),static_cast<uint8_t>(best <= original_alpha ? 2 : best >= original_beta ? 1 : 0),best_move};
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
static const char nn_blob[]=
"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Lge8CQCHgUACtAIG/8pBxwb/BAaBoTwDAQErFYL6/v4CC4FfGfr8/SUW788EAwPV4ukwAgAFIIHNEf4CAzgh8ecEAAM7yn/KCPv6kpsouAcA/ubaDCT7APrV54HVBpfaMFsLBPz/9n+nqLsOgfTGBh1D9PJ79/8L/+o87vz6gdfhB9UCAp4XHAAU/AQIohiB/gUA6UHh4v3/As4DCJwEA//9uwuo/QEBCRBBgf0TBEHIAN8FFRh/4JvX+vn7KOEQgf8HBNPcxyQHAArWgQIfAwQDUi80EwUJBpsqDYH7A8E1nvsBAADc05IhUTefgfj15P8GFM4M5esD+/oDAAF/Ae0GzOwDJgz6Aej4+Wb9wh0J//sfGwr7BgL/zH8DgREC/3boJCALB/HfnJnM+vL4lS9vlgIA/MnIf50N/v0H/roa+Ab9K4FE8Qz8+OP6qtMB/v9XY4Fs+QcBE7z4tPTTRvZJ/vLz6gfQf2ldGzsW/igPA/rAgd/8FgH9z/0W938K4Pfy9RH8qS0E8vz/pRIGNwYHBYFGYv4R4wLgLlB3G/gE4FtL9uj1BcZ/H3sA/f3fAEQi9wn8rH9F8gvxCoFWAMbm2/uF82Xl6+QBz0cf7hHt+60WS8zs7gKhf+n6/gER/vYKAfr/GeB/M+X0B/EEGwADBLOB+v8hEAPT9/3+AyozBgf8+oEL6gfIAQRHP7FG/gf364GO0/P+ALyGLF/7/Qb7gSdUA//81vEEFPr//d0h5uEH/fCIyYG3/gAIB8SBOwgF/JxH7fsI+/8f3oHv/gP/A/7t3v3/AeQfzkz8ixcF6f8G9AMekvQG/H+o9d708hPUwSKBDBg98g8M"
"yhruHXEZUhqAHWseHSXyKBoVBhJJEkAReBHbEnwlGypfHL8d0hp1HTwdFxweHcsqdhxeHWIbsBkOHXkcRiYoJ18TUREYE1MSIhBfEsETGRRmHrof6h8ZIG0drB9CIKAi9BhqGmccfB7PHV4eOCYmKQob/CCMIVsk6CDXIechAicqIDIgUh0ZH/wcMSLjIPwlARvlGmsaPhwbH3gezB0+KA8ZmhfPF5YVZhVPGpcZEx0fGi8PYhDmEFoQWA3+J/Mk4yCJHHogrSB9IIYgfCJSKM4cMBzXGRQewRmmGzAmWCqjHvAhXyKcHIwf7iDQHxAmWhx4IRYfNh9WHSEgmR3lJF4jUCYrJ88h4SbGIs0ngyWaGB8XyhYpFXkXUBigGZob3SBCIV0hPBwXHgQgkiQKKGUgjiCsHzkd8RtcIM8joiZYHsshdSGcHxUfZCAQI08odhyNG3gb8RjZGWgaTiKUJNUecSJ8IRoeFySiIZsh4CeIIM8e+h/UILgkNiFAJdonfSDSJj4eShx5HishoyFoIi4cgx6MHq0aUCD2HmAjsCSZJPEbaBs7GhAbvR0IH8Uh4huUIPsenh1UIKEd+h1uKdIcIx12HxUeSB84IEUd0CPDH5cg5xw4H1Ue/B47IHIpAhsSHpoePB5CI7sgQyFcJ5IeiBi3FeEVoxTXFcAkzyPrGXgcLx7tHJEdxR9tJJ4sdRo/Hmseax6yIHAhNyHAJYEVgBQwFg0VVhQpFDgg/jAEIUYakh5MGk4dlB/CHuwnxRxJHZ8dTx8iHbkcLyQQKMYiASMGI2Qm4iYzIIQjzCO2IBoT1BACEIER+A8hIk8q1R2hIeUgthtgIHgesx9MIcsgEyEjHiUcTB8PHn8gdCQTJe8d3B2nIqUiVCE3JRUq9yFxI4khrSDRIPQdpSDbKCsgKSAaIygihiBAJfAdzyQAGfgYpRjVGTkZJhlFJyghCSDeHvUeVSGfIUcg5iG3KTEcShvhG2cdFhnNHDgmXimoHl4gkh79IWAgXiFLJN0pCB5RHfwclRuCHgAeVx/XKxsdfh7XHWkdvB6xHAMclSqKGRUULRb4FBcVxRTQI+wsixdEIKkdQh2VGwIbDChsJ0seah01HuoeXx0hHu4h7yueHXoggSDtISQhoyBdHm8nAB7yGGQaLRndGRoZeSZAJqAdYRbvFkwXeRYcGEYl/SpQIzsf6yCPIMUelSMMIRorORtZFlEUyBZXF8QW8iqNJcAhTCIBIK8kFySFH5gixCICJn0VjBRMFoYXmhhZIyIj/R/jIhAgEB44H3cg7R8zIiIaSyCtH3oeFh5mHyghLSpqHkkUexOFEy4U3BMPJvEi1CDxIFkfDSPoHw0jZCDTJLkcHRaqFZkVIRXlFVom+SZUICYc0xtOHX8akhxKH2QrXiGHH+kePR5lIH4cOh+HJ/8c8R6iHlkc/x1fHrQfPCiQGBgXZhd5F7oTPBQFGnkbQBuODpsQQREIEDMQ0h5oNKIhaSCEIk0ftCOfI2UixSJJJJcdjR0QIdcdBR5RIf0mPSBfHVodTCBgIG8azSKnKOUUZhb0FNkTnRW1FAEeGC5YIHwfbiQLIXcg9SCCIuUoDyBYHIscyR+uHIAfdx/bJk0nXBhaGfIbLhjTFiohDSQmHecmKSJAIjYfEiD5IMYl+R7+IU0jqyIYICYhqiFdJ+4g1iRUJOMeCiHrIHAitiiaG8MhzSEPHpwgCSH7IFcoXiAnIgUhvSAyIoIg5CJTJeYclxwOH4QcYx/bHFMjwihNHMMeSCKbJHkf3yQLINEjjhq9IB8izSCAIZccrx52Jp0buRxtIHwd3x99HXAkqyf8G6McZxt9G+QZPhstHGwr/Rv4HAYgFiHlHxkhKh1QJzAfHCCqIlseJyJGIegj4CaHHRQeth6MIR4hWSGpJC0kzx4VHB8ZShuOGuoabCJUKFsgtCCTILIgXCXmI00gMCW9KKUmJiU5IQkoiCjSJBEhRiCUGY0YmBjVGIMaKybIIeMUIBzCG3wZSxzyGnweFSQXHN4gQB3ZH0ge+SBOIFUm"
"mx0PH00b5B4zHSYh4yKpJv0cchy1GogajBg9GnYf5iwXJMMgPyVKIUshYiEqJR8jkRyeIGUgDRzWHe4fJhwIKK8gTCLZIIohKCBZIeMgoiWiGzIj0CGnIG4hHSEmIdcnAx5CHOYaAR2RHSkfRiV2KeIeexDYFHAUxhDBFFYnFCjoHNId6xxBIDYgxx1aJGopDSfnIaAgdh1AIYwjNyP6KxsaWB77HSkb/RhtGwYgcygHJIYgUiCIHRsgXBn4IQgplx3pIZsfkyHrHPshdB5vJrogExdcF6wZQBeREbcmbSgqHxofjxyhHuMfvh/HHRAo2SNTIC0gsh6mHwAe0SCtJUcgkSASIIEdLx1IHVQkVCQGF+ESohJGEukTKBTpJxInzB5kE14TrBb2E3EQQSYTKyUhGiJCHisg7iB7IdEd8yTGHfUhHSLjIEgkUR49HbsrFx0RHgYjVh8/ItMfvR/XJckWtBCtEBwRMhCLEMYlNSOvIPQhGiEwIZYjZB8QHyEm1hmWIJUfNSQuHnMi0x5IJrQgjx2DHc0d0B1PHCYk2CQSHlUaXx8BHZ8fsR0yIKIjmxwOHSge5hwvHAke6SArJsYm3RxCHxkhyx7bHm8kHSe/IeQczRyUH88dMB0NJFYmqx39IbUe5x4DHf4lLSaBJxgcCiX5IjgeJx2PI7cgOyh4I1sgHCA5HvEdFxypIA0pGh01HfIbeBmWHQsdUibKJpoc1xxxGQ4czxmmGjYg7izaG24iGB/xIYkc0CGAHrgmZx7/H5ghWiD1IfchKiFWKJMhbSEUHokemyAPICkgByZfIK4eUx0VHtsd0xyrI7QnNByeHIAfjxugIcId7iOLJJoYKh9ZHSUfnB3UIngh7iV7HUMe1x/GHZkd/R61Iskr8CBgIAEhGB2NHbcfiyScJxUYNRaxFBsTyxQfFYscDC9aHCIjDSGIH1oh9iH0IAgoSicRGHwZvhy9GO0V2iA+JNMfYh3XHiQdBx6UHZgfoitFHLkgSB9hHWYevSY6JoEn9SSIHaYgyyIOIpUiTyUcKsQWXw8vEeYQMhHPEGwnCCVnIxYecB3QIVofEh7DIIomtiCpICoivSDQJQgkyxwhJZ0dBBkUGHMYxRj+Fy8nUyeJHlogyh+CHpYd9h+vHzckhxgDHLkbyBzrHBwcTiZNKa0ipyAmID0jwSJ2H9shBiOIHeQg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Fx+8IZEhXx+uG7EiviAuHwAlZiBoJvsd8hyaIxshQSDTICcmxBj+FvYatxq+GHEYuBbKFMEUBxS2FgoUpBnFGfcYbxqfGKcaORrfHDQY5RQRF70W0R03I/kfJR71InogzSEbIvogbyATIFUiayA1IIAhFR10IGkfoCCzHFMcMh1+G4ceLBwJH9IgaR/6HnYjOB/NIYsfuB/iHoghHhG2EsESpxPWEtgTxBPTE+gOWg8FDjUQUiBWJJ4ehiIVI+Ie6R8yIRAglh76HjEjSximFeoYsBfjFT0WdxIPGFQSGxPDFBUUeyMlIy8gpx68J3EfVh73JMcjcSDMHi4oBR1OI1YnKB6lHT4gEx8tILsf9BxBHfccIh5wIhIj5yBxIwgfOiEkI2ggJx1gHjAfHR14HAkenxoyHuAeyhoXHpUaaxhSGKcZVBoHJM4jEB0QHigfBh7OH4cfix8zHT4c0B6fH20h8R/rIVwgMCCrJHYcUiA8H2ci3iGQIIsikx7cJ8wduBwsJHEaLh0KHjsnwiXZJHQhIySsI4sgyyFpKHYeRR3OIYghHRVLFaMTAxSRFRkVFhTQFfUR4g6fE60R9h9zI4smpSFAIJkgQRuFIs0gyR+3HPcjrxv/IcAhDh1mHw4kMB5rH6Acph77HTke+xhOHp0gzxxjKBIeHSDnIb0fTCADG3IdkhiPGggZcBoTGQwY/BYGGXEUehb8FjMVfyFsIFwh+B/dIhMipSASInQeMR+5HwgjehX3FgIZBhjGFlkYUBXDF5UUiRJfFbcUsCAgJA0j3h8eJAEifCF+H4Ec3hyLH0MjUB3bIt0hSx0vIgocuBv3IWAcvRvTGpsk"
"Wh/RIDci9yD1Iv8ezx8qJDoeXh7+HvsgzhRxEgsTqBOcFAQUGxT5EtUNiA+QDuAQYRWyFHMUvhEEFO8UHBXDE3kQqQ6PDmsQnR4kIUkgqSF0HvkisSKAIsQefR6DIakcvR4yIO0lFiKhInMgLRyLHugc7hs4HUgeYx4pI5EfBSC1JAkeGCGEIa4gHiCqH14lGRNnEq8SLBKUEcUTMRESESAQeQ4rEekM7x+iIPUgiCGtIBwhIiMnH2AgRB7iIS0fVR9HIXUi7R0RH2EfvB0PIYod7R5JHTgfZx43IQsj4R2EJh8cdxyWIpkdMxztHbskQBzwImEiMiAXJAwdAx/2IAgeSiAOHowjgRryGTkdeh3rFiwdbxnHGJ0UMhVwGX0VXiB4IK8fBx+aIvEaShwlIWIdEBuWHlsgWiD2Hb0kVydvJYwcAyCFJWchHiBFGiwmnSEQIwMkER7iIBsh0iFWH/Qewx+cGn8g1iA3H4wmBSA8IR0ldh+WIGAf6R0SIAUgJSKcIrYfyiQhI4Ueoh41JbcdzCHQH2chHRG1Er8SpRPYEtkTxRPVE+gOWQ8FDjQQ4hqvGm8cwhknGDQcFB33G/MVKRYBGGEZQiMnIH0ibh6jJqMewB7PI0QcFx6zH6om1R2WGzggbxreGz4cBhjCG/oYbxoGGLwZTBIKEdYQvBEFEdwQLxKwEBAPXQ/tDiUPkiAqJCUjoiD4I/Mf8yHHH00dWBwpHzYjUCD+HXcmeB4dJK0edh4KJPcgSyG8HUUjNx0CI5MitB90JO8cHx+dIOMdwyAMHi8kOx7CIJMibCAYIgYewh3dIdsgvx/xHKMhCxtiGH8ZvBnmFp0Z9RfKGBcWtRX0FyYUOCAZH/sejBoWIOIdqRzqHQAiCBo6GnAhcSKJIcghpx1kI1EdqRxQIZEezBz5H7IgWiIKJHIhgh2jIEYhXSG1IQchtyO1GUwg/SDqH0so7h+QJRAcCiGCI7gh4ByfHdskqSFxIVkkLBx3Jv0cRSAlIVkdqR1NIc4iKyILJXAiUyUJIcAf4CF0JUAlwR/NIewdnR8UIXsihR1AHqwebh2aILscBR4/HVQeWR6YILIiMyGNIbkggB4eJKQfGCGIHVgheCABJMEgdiE4JCwg6CDJIOoeuR5DH50jJR63Iq0jkSBqJHoe/CFUInogbiBVIJ4gHR14HAoenxoyHuAeyxoWHpYaaxhSGKcZgRrxGTkdeh3sFiwdbxnHGJwUMxVvGXwVhByxIWsheCDPJPgc9iCgH30gCCGbII4mUh6MIdci8CFAIasd8B/EH0sfLR50HUkgzR11IbQhoCFWJfYhNR5KIQYeJRwVHaclMx5FHwog+CAFIxse/h1xIeofhx1rHZgdrCA6IO0hpyDfIt4hNiDcIYIfnB+AH/kixBj8FvYatxq+GHAYtxbLFL8UCRS1FgsUshxAIwcghx9gI7Ag0yFsITMhpCAsIMoi3RvlIA8hdB48HhUkfx5DIYAfMx8yHgwiDR99H4YhRh/LInghBSBvJGYb9yD3HQsk7LJKrMO3RLgqL+mz6KAMqVG4rrzeuQ0wW7FQrKO70q7+tewxvLF+r/AnH7xxONGy+LNuth+0SbkIpRe1dKnwqMy6F7IJuE8xprpjsW62N7BNNBu9pirrrvCtXbBEMYy377j0qO+ulrqptWa5aq/nrHy36qjTHbu0UKoSsHowL7X2uAil7LIGsX6v6KDtM1M43qQ/vLyxkCijt7ewwjQurZa8irf3uAOwNrEJuLu0GbT3tb+6m7tLrdGyMLZUuQC6Jw/xfx8ZABXXhvMROw7wEsxJdxb+1XFVhyl/3gXYSnjhfwsA6RlCFCscK/bzLgsm8AIG7hH2APU1CAURf+wJ+N0CPy8J/w4gEu4zf+hDJCaBeNrjLxJDIejp3+ctF+jXfR7jFlwdfiILHIAa4ko=";
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
static unsigned char raw[49070];unsigned acc=0;int bits=0,n=0;
for(int i=0;nn_blob[i]&&nn_blob[i]!='=';i++){acc=(acc<<6)|(unsigned)nn_b64(nn_blob[i]);bits+=6;if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}}
for(int h=0;h<192;h++){nn_bias0[h]=nn_half(raw+27648+2*h);for(int j=0;j<128;j++)nn_weight0[h*128+j]=(float)(int8_t)raw[0+h*128+j]*nn_half(raw+24576+2*((h*128+j)/16));}
for(int h=0;h<96;h++){nn_bias1[h]=nn_half(raw+48768+2*h);for(int j=0;j<192;j++)nn_weight1[h*192+j]=(float)(int8_t)raw[28032+h*192+j]*nn_half(raw+46464+2*((h*192+j)/16));}
for(int h=0;h<1;h++){nn_bias2[h]=nn_half(raw+49068+2*h);for(int j=0;j<96;j++)nn_weight2[h*96+j]=(float)(int8_t)raw[48960+h*96+j]*nn_half(raw+49056+2*((h*96+j)/16));}
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
int selective = use_futility && depth == 1 && beta == alpha + 1
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
*entry = Entry{ key, depth, best, best <= original_alpha ? 2 : best >= original_beta ? 1 : 0, best_action };
return best;
}
static Action choose(const State *s, int *completed_depth, int *advantage) {
State root = *s; prepare(&root); s = &root;
deadline = turn_deadline ? turn_deadline : now_ns() + (uint64_t)BUDGET_MS * 1000000ULL;
nodes = cutoffs = hits = qnodes = pruned = 0; stopped = 0;
nn_begin(s);
Action best = {WAIT, 0, 0, 0};
for(int i=0;i<nn_count;i++)if(nn_ranks[i]==nn_count-1)best=nn_actions[i];
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
constexpr int RNG_CANDIDATES = 64;
struct Tracker { State states[RNG_CANDIDATES]; int n; };
static int same_board(const State *a, const State *b) {
for (int i = 0; i < UNITS; ++i) {
const Unit *u = &a->u[i], *v = &b->u[i];
if (u->hp != v->hp) return 0;
if (u->hp && (u->pos != v->pos || u->owner != v->owner || u->leader != v->leader)) return 0;
}
return 1;
}
static void track_observation(Tracker *t, State *observed, int first) {
Tracker next = {};
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
int count, turn = me; Tracker tracker = {};
while (scanf("%d", &count) == 1) {
if (count < 0 || count > UNITS) return 1;
uint64_t turn_started = now_ns();
turn_deadline = turn_started + (uint64_t)BUDGET_MS * 1000000ULL;
State s = {}; s.side = me; s.remaining = 150 - turn;
for (int i = 0; i < count; ++i) {
int id, type, hp, x, y, owner;
if (scanf("%d%d%d%d%d%d", &id, &type, &hp, &x, &y, &owner) != 6) return 1;
if (id < 0 || id >= UNITS || type < 0 || type > 1 || hp < 1 || hp > 10 || x < 0 || x >= W || y < 0 || y >= H || owner < 0 || owner > 2) return 1;
s.u[id] = Unit{static_cast<uint8_t>(y * W + x), static_cast<uint8_t>(hp), static_cast<uint8_t>(owner), static_cast<uint8_t>(type)};
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
