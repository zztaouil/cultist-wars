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
#include <math.h>
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
static const long long TEXEL_W[3][8]={{1000000,60419,8180,3118,1139,2840,9173,9244},{1000000,58192,839,2438,1791,3071,7465,3049},{1000000,18556,0,0,2634,2865,0,0}};
#ifndef VN_ENABLE
#define VN_ENABLE 1
#endif

#define VN_H1 16
#define VN_H2 8
static const char vn_blob[]="3S36+QbuGuN3Kyu9F6kI4EXvM6QwxPv6Kxg2DwoM/Q0/MxQETBrzAQHrORT/MfG+/wD/8UPiBBkVHyRDNy8x0xMXD+Y86Ms2CxFAJAIsBc/3If1QJw4BwjXkTivvSybsnTA3BnYmJb9X4hExB2MXMSwOHj4nDM0STc7+K+KxOxXpOsMESrDzAxvlKPv86Cai3P+gBeYV5gks/wYD/CHxyOb12RQr/soUBdEm+f//LbH9K/cQF7zVNTsc+NXp+wEcADqo+x+ktP4w2Dra/+Mj0AbKw9UJrjiVA/MV/xTUHPSwARb6BhMiyhsFFBUlySX04Bge8vT9/Jb24OXBKN8A9ykjF6v+wScD7yEBByDdNw8YGAkQBPklCtkSAujfGtz7Pg7dE0e8/wUq2fBP8kk43Bjwwww659H92r/9GscoVFs2POwaHhEFyMQe5xD5JjP++S7zDgju7QvYFgEW7gksFO393+UbCAnpNgQIAAlWELPVQekAHjwBM90K7AzlCyC28goH9w0BEyjvC9IP4zsasgYGxPn9LB0sIf/x/xXupvL8FdPY3ekqyCca+uvT1UQQBgIe1RwYKc77D98W9xnqHvjfHgcL6wDO68DV8Q/2Gwvr9RjWBvo4mQcvCPr/ECr+Fu77ByrP8dMnMfLJwdwNGCv20dMW4fklxvD2+8z7CjkoPuYaEjUZ1wDbHQrbV0AVFd78BQQhCswIH9vj9Um76wwJySPhuO4cAfoaMNlW1v7t9+UVOvsDIjkIGAwNJO685hsaDhvfGjAmAfsmMf4PoPEwvRAV6vw8AwQd+jMyTMjpADT+y9oTAQUV1xQjEvnp3c8r3OkgGJUJLtjp0/z5Es0wPLkMtwrGyQsA1sEKDiD2FBLUEcXwkhkMFh3Y/0EV+Cr+0CPqBcb/C7fr+Enx3y4M/O867ciB9ucdIbUAJfrxLtDy/Bjh7xzJOjG2SBvr1xfJ5xY4OfUGDg4f9RgwKvoQxOjvDPG98BMG1uAH9wINE6cK+Qrr+griCxv/3xMF9ALy7xzjF/whGR8f8w7++f4Xuwk10hkrDNoq+/4YE9Tc3K4VEgDmKwbnEP8fNgTS8PClEgEFFzcDMwvhDhoqHi7zBQkC9h/gAjXl+sHR5S4BvybN6unq2PUI0vH3AAr22Bom0SQS16QjDRMrzYULLwz0It4t+hvM3/fwBsVNLiQkPPkNOxYF1wwSI83pKjUADSkhIRcz0N7c4Svt3fBB2Pn50OE2HiT/LBUyHBs6OOUYK9XsEjUg7ASvAQ/fBtQUGh37+x4iGBIHyQ4yDA4CIPMuGQ0MBt4AE7sVGBXP7jHsFzwfAyULIk/1RTIPPFS4FSW0ERcj918FqP01LD0DthY939wPHu8WGNECFi4wKfa28xbZHur6wUDpTAAC6U/9Hwo6n9DUINQY6Ar8J74R/o0LIbwm5grIAd8GTv0eDifw2ToLCeb6GQYGDOMOECZL+AkgLUHaCBPNBybz2FbWGeF/CBZB4/H1Mw89/twX+gERPuMwJRLbJD0KJ0wKQyD+J/npGQdX09vxKyr6Dv1vMjQKyPgd3NP58vgUCP7xKvwtApkEIf4KAR3uATI12Ssi8DUM/A3z+iUcFOZJCzIQ7jEwJgPrAd4J8xgbAPFXFg0DZMr7AO7tEU+/ML30NQvrtwwM4grBDuVU9wIMGesF38IAKt7kxeQN9RUOKOcyFNzp/vm99/XoFPbvKhkuGzTs2B8b6yO0/hDXLDoW+SD2Fyeq984n2OoWwfkT6RNQG70vBQEQHNTr9fbnHCPpBBL/InHWEUH5vsUzCRRFC0dk+fYRqjQCKY3tf4EPPA3BWbL14QsMMO7K5hLJ7Ak2vxkQFfcOGjy1Qey0sQbxuvEUN+PvCtwW1g4bF+L3EOcgLqz04SEG174P1w38/Qj+LBQABA4YItnHGyAbuC+YH+4j0Lf2976lBBTn7x4e0/8Mw7nvDSjk5goHyPnSIAlR+A6h/PEg4OZB7fb9FuPTR7YiuNruC/MqGf0NPv0O7en4zgwGEA/lNekI4QEPJ/kNG+jBDgwi+T7p7yEwADQH1zrs8zIaCRk1I67s1sjqGtoyxR8VLfv/YZv/BR3yFC0CPR68FcCw5RMKH/DiDQz33/oP7DLpKe4bDSnvDv/r8wTYHtUvJeIaBuTPBvkg+Q8G6v3IFf0DGtQC2BhB1fkB4szxDAau0xXKEakU5fjqgdYMA93syQkVvy0eChMC6AcvDrWoHPb+Af8CEbss+CkBTDbY7PwnBwov2CgCMib+CB+/CfoCABHzDvksExET+uYDQQH+EOAJ0SzwzNANCRv71D4vDTAcyftH/usGMe8JU/1A+w0aC+ACViUU5/3xCj8SUCgwFgXx71/4EfIa8j1FCgMxBRkP9PQ95wogBiIZ8OQM+tkTDdYDSeDhIPjPHwwCGR4ZFtjoDAT9/yoiAeDYBP3/4O5ACBwJNoEjKeQlJCT27ALy4Cf32jopJAL2tPgUGwamFgsvxuUE98Yuh+cc6PDn/x8GRpvMEwDbEv73+dLb8ODtJSzS8uA37zvezvLc1hfwuEv61fqxN/E9CPoD/icEqPMQEOoUEyP7Fv8uFRFTNOEBGuHDVwoGFwcZHg8KTj7y/lDi52IuyO5H7Shi91lgCyoi8Bgw6BjtLe41LwQqHxUOAhzaIMkqIBkPIgUkJQPoPvEJ7xjrAh7C3AkJ60ZE2/8W3PgA9OMh874RG/vpAqIiCa4AwxbFMOwy9PQpjhzHvEOFIt/B3lARqy7xA+QJgSEFFtXMkez30+T7CjcPyaDVIizCtOZZHy/ywNP6LwL+9Q4q2fEXEvII+eLrCxXw6RAaCuP08fL3Kt7xMwroIPMwEvPwLgrxEv70BhwRGQ7tQy4P0UIc2fUaCxs49hhOEAg8Dg1TB9P7FgMpOfocUfYrLvAZf0AzBk/ZMi/36UIpCxkXMFbMGuhbGwQl8iYhBz7r9+X3PP8rA+4C2wMS/d8gFDkL++T+GQH63AHqIQGz9gmlHegZrhgT4gAsRUAXCNwlgdHe8O/7RsYBZTHcIxwCLr/O2JXcICMPJUdm6SyzrP8kxdokJ9MZ+9AAM77wF+YhDBT07uIz+AL6yzD2/fkgBxbZE5z9C+b0LvPeASMVKCLv3A0K0cPjDggu4SoO1zQr/fdI5+P4JgYvPeYmJwUbWOAMXA4FGDDyLWT2I2kLHD/g/24PEggBzSNJ5zI1IAoI8hEw9hYCKeIsMfYaEh35EQgNCN40Gt46BQM0JhmzHhXkBwGgAgjsHBjz5/kL2AYP1gbsFM34TBPu/PwWr7/UTOIjFs3U6lLgKSMSGbml8xtQ4dWeBAX80QgmS+sQ0LQWPL3RLgjM+MjE+BQN2OW4CMEF26zf3FUX4+IR7hgDJRwI+hnPMfNTDQPiDC0PFgwd4xNJHdbJGgkDGyYhFuwTTALxVOgP3xjYGBkFMT7mIy7x+FMY7xIeszY23Bwz/ys92RdsXAgaNMUVOPD+Igr79vM2ECC79wnuHu3SL/0wGywxEPr8tyXxEAvqFeAH7v4QvhQHyvAFbvUdIM708o4AVPMY/ufzMd3APAkmR+Wb9Rux/QG1xhfmvAYQCj4io8AyIujiifgg9DUH8SjPC77sPjZA6wCpAQf5KxYT6e6q/Br9E/jB0c02zfPy8/v87DE28xj1o97MMxpNHQM/Gewc7vMP/KPU3TL+7TDjHQ0NGgsRG0y8CNhGA/s66PEoJgAXFgdFxtsqGdgvNfFONTEWG9UcLQ3NFQIXACPUQlQBBzkw+THBGA/S1RwR/iozMwINCPH6qxfevSkY7AYGBcL6HR4b9O3WFPkSAiUY9QfCwE7A+BALswPeG8UR9BjVEtEV7u8GhtQTA/noBUpb/cw2LyPVxhCqGMu3GepS/NnX5wHj8eTWJxv4DenZ0ucLC+gLN+Mf8BVD+Z7pCiQf+lUHBzzB6qEcFdP2/vQrJt0i6x48xTU7BgDkHhL9K+y8LqvpH872SQgS+Er4654A5jy44DPa5/n/BtPFLikK1ggvpL0EA/A42R4G7zoV/gdODzWZCO3xPhQnHlme8u0r4SQYB/79BRInzwLvze/SARckJdnyFRETFyTk4ucP8RNXOPQbDiIQ/fMpANjPCfVPJBY30zYM7QAVJs00Bsn/wrrcI/MCEc8d6uP3B/K35euB+S0fDAsL1+Qh0936CN1U6OP59NwHKgjyCBYECR4hFePHQuTgCBki3xn3DP0pKj7u+RoiofgN+r/4EfwjJjPzIgroyOQ7/EQj4bIc/8wUMeIy0tAz/P4YLBkaBBkKPPPH4gXlM/3YOQsP4eDw9xwW7QQZFSDD3TUQTSYJA+wPIQrO3gos9hAoHwwGLeL6L/Lp9isZESoi4RgPRuC8/+77JBM3AAI8CEXux+33/v4YtyYDFvTSNRn47+gN2vsa0fsD30YT5z6uC/EyKLLgAhkLtdIFK+IJI+IRLSXCIPARCwYHGT3RHEHT5Bgl9hs2JPEU5AgQxRMd0wcrPgQ+MBUKH9ng/w8O1rv4JgfnBwvs9xjJnhbl8Pz1Gv8x/foL2PwYBqbv9RPKMDTr0zXb5BUDyBAIxsvA3EwF++Mo5AAvAuvr/vkc9QL5//QRDf39Ow8UDBUXAecfwBv71Rv19+gCN/bwJQQRJgUP2hP0rOadDT/6EugPEDgnzAxLDfzi3t/8LdlMHdYAzM3kNN0I8AvVVSQrCOTKEd4W5RqX/PhEKvX3CxcDBgLR5wbXAiX8Awj8ChgDASMbBBUv5P73Hknw8uEUCNYDFeUAM9M+BAkh+A3p6+H7JrwZ4kP5CBHXN/cR7c64/Be1OkH2uTLO0Orq6C6d0Qvp+ikpz0EV6QQT2fY08OXz5Of+NOmy+AL21ALeLfMVFevw+RXo7yMnEg0W6iKQ3DQLKSwD+iLt/MMJ7D4lAQYyzezu6OTyAtves/oVGiA5/sQ0Dgz1MvfvIPgLMiPwOvogJ+TgG0HV9CTDEwcFGiwEAeTBE/zk8RwhPxYMuxg8Chkx2e4EH+LZChg/9/oB8hod/RoGEQXkBe0RIknkCQnbGxMxGzjLGRQKI/wu6PInERfb3hUrM+4M7jL3/LgA5egrs/MiBiPtxb7m8tz+ztEYFypGKKbxMBAHAhXXKosK5T3MUAIQ3xcWBP7y4fKoCPUB98Pz49QpDPwG7wgQBPYy0QP8FfQGCB/m4gslEB3/BfNQJhoeFRsI6//PKhETKvnOHQn99AYA5tGsvQ8eFRsP+NUCBxXvENXoOtC1BToVNhHk1xoQ9uTzCRbxys5/7eEXCPPm8jYT9xcs8J6zQsDOIxUyBhDnHQAFLQ8H8zsMNxg9Heft7g42DBUgLvX6C+b6EuDuFEm3CvkTpU4bBAkq/DwDBDbyySgwNf0yBbToCi05ZvEJxBUJ9834JuK03j8g1NtE9zIY/vXwHAO3wcYkMe1B6x/Z8APKBO4KIu/HMTQRBhj/B+v298cyIg0LBwsLIhnpH/wK3QLi3BUovg/4re0n+DfVEgvsGsEo7dgWAO+30gbm+RIDxPnitjosBivN5+HmFM+y0O4b7805PuYN8B+WPhsKMA/kAt/fPPz2Af7j4j0DEBLN/AkU8SASxh4yGkgq/8sdLugLKx5BFe9KHRcFTBMY+tb/CUP6xvTmARzeIP8MH+b/zx4iCP3l2S9QHeIrHeFQGuQl0NbgBiMlTdgsIODr9LHoAPGo+x/HEA8T/ujqzK/uyAAe7e0Hq/oY794OIO8TDR/tC/b1DA0RHwwEFzTb69URFPbVHAQtwrIFLy/+Fg37+BkCzCUsIwT85YHiIR+twO3E/bqRTDk9vCP0CBBD0dSu9yjdZUjWggwS2/rn6jyc9eao8SsnWTkALgvV0fgHBOkE84wE8hkh8Rb1FQ0lAz8mJycMKwvIFAsAD/npqTnmFwEJHjsP3CH9/1L3DeglGwA23Bwl6dQuGxwiHPK6KiP8Frn35eLITCQWGewz+hzk/CXVK98K1C8VK+wELO0Z3xUSCuXs0AoH5Bj+9yPr4vn39N8XKf0bCOoI+Rrx7w7zzue37bso7+rN71nR+J4QDdnxHtGzHkvb6gbvKelFtdUZ+5EHCyPozssw6wz7kQQbCewX5g8g4Pbi50nc6MEe+vQZG8A3B+Ab1wgBAhDGKhX75Rn04v4kKOIa/ScJHvYIEvMa2BL6BTEKASErKv/0CccEFwomwuFMAhRXDiz9IRvtAOIgKrO+NBIoMxUCyTAs+foK8gO63kDsJkr8BcpB+eAL8Acq7gEhExUL8PH4BvX5DgwmMADkF94DEQ9HRROe8+oiEy7KJ+bYEA8J7w/13fzbBB77Qfrk5RuSBvj0HtzU8A7zAvwy2Q0GJRHvNbjrItmeCeYdv/7gPi4GAdId8QfjgdryQR5UERAaKNvsrgIM8pcVFSswKQQJDgDz7ATq2crUDvMX/OMRDyH/ErP36fkCEgI4FwMGADI9/B4AEuXa6Bn4VN7gIws+OToP8Qv6BCggCifDzy8oHkkxM9oy/uUM7vw6rdsTIBBhJuj0URXsDhr8/OnyGv0zEPb2zSD18eAA9RwY+xIPEwYjMwsz7wwZ9uAgItLv1gk58fTtFpbc2dvB1lUy7fQd53L/Du8NzoEA1RVdHegLLbTuRfrngRGmBtIkPePCE/PRNfTN1boG/sK85k0LCQD7BdriAQH+8O/aIgQSOtMEH/wi4gcG6ejt7wnlBRz0GgAhCgL7G/P04vLlA+8VEgwdCgwg3+7+BBQiFh7uykgRLm4xIfQvFfwiExYP97o1JyhYLyTTag75EA34CLilGygPI/kGvPgRDAAB0PPC0hwFKEPeVBfqD/gnFBoc+y4CDRAC/vgjKBQLDvYDDvIi9O4IJ+MAWQHV6RTbEv9XIQLaCCAv5goQ+/bl6woN7EEPBeS9NEQsDtfgvS68K1ICBB8BaEfzkO676/shyh0+zwnwGS3m2+jWAdPUHg8ZHyre5SABCvLG6eP0GvQl/RXuPw72Gf4U2AUDACT8Hj7z4T8M+TkRF7ktCtcO6e8gx8YfGD0lOQX5XPLcJTPpENDLLSgqZSEq/FXxBA8v4g+y4hs1LBosC+Jb9/cwEfwv8PQj8QnOCiPnFvfn/xrlDvA95f4I9tT+PyXp0Qs2KSr1GQ3cFKMjFiEl0+oR8foaCw4HwfcGWbJS9gIVHd8sFxUBC08Hy9ox8fDuzize2A46B/LhRd4g36766Qbt+/IrZ/sb4SzGNfEtBiUjDuHNGC3vHvQF2SQByC7o7Ovy8PkVC/Yh6vM89cUI1/reFv7Z3RzuGwUNRgcL/gHzEPHiEumsQxj7FA0O6DsAEwDmwy3juSQLAV84HuRAAfkNDgML1OYcMh1HOwUGTDwJGxMPE+/v9gwK6f37OS35LTgmKQcRPMjzLAAbEg0DEPcq4iIFBgscIfYhEOcf8qzm+9D/JjT+8OYy4hoi8ibg5gno8Bgi//jlKPAwFPlBs9YU4NMdItxPgRviSRzC1boPkRoYxH2wKOIPNxf67yO71tiauE0d0yjbBSIx+cM56Lwv2d/rG/1Z5Pc4RimcD8zJJAP2SBz5Iv3o6jQW+xkF4wvt9DDi/TYJ/jHv/+oq/goFAQ0D2O4hEh4cQQITUgXq+wkfGs3rRRYDCzEpDjQNCQ0dCc2yAgkeBvsp+EAhBPw6K/wR2xv+Kv8SMRUZAf/bB88Y6tUnxxcG0c/QGw/W3NOv2OwDGuvfMBTuMBPBBwYU7rrV7025B3IvOsoaEbkpE+0d7hQaM94fFfoIvhEW/ff2DC8R6EHfAsQHwc4e9OPqBhMc/N8h2BkfE8QCNQ3mCh4QMRXoKd8BSBXX8QgDCjkVJiEEEjMG3PUF3P0cAgjo+fse6Pj1D/kPBeT1Ju4G1/UzEfUcCg0BD+j/IiIJ8/bVEvPq7gUcJhXq3gojIPXo9hfbwRzsFzhBCRceGgT98Q4r6Zv55xQgEu0NJw/w+QD2HPPWKQQcDzkdEiTxFAYYJP3y4hfdJiUzDDggBOUQ+jv32RMl8SD4GvZNMRL0IxbpHdsH5z7QTjbZzNMNBgb35uouSAs3/A27If7aFRrp+Rjl9gvjBe0jHvbd2C/zDR0G/B4FDxvg5wjz9fIsDvz++hr73+QCD+c16tcYK/bfx+wW6O7mLgYRJerjBQYT8+H2GBzdG/j9Avr2BRUi8vz62B3sBwr2+igrCewi9wb6H/oZ/+EF1BMeD/kSDgQVLeb8A+rJH98cORjySd3nIvT1+CYLiBPdMVoQ9CAzGeP5Cyg2DsUszi5LNN8WFfb1+BMPI9S/Eb07LR2xGRUCzwv9DTTnRPNKxPLUHMiyOgTexer2FvoBYtQ7EfDd1ukeBLDF+Uvt5jroLOEG1rcVDQeo0xYJBiQO6QIMC9ffEvYK9QgEItre0vo8Fh/FCzzw+OUM6wnbAPwLDBxB6gDrBxbD6Qr//xH9BQ8VDPnz+AUV99b3/RcQDg4bOfrh1wgVAg7nDfDkDMwT6DYnHiPtDegNCSfzsSsLBUsOACvy7PEe9AMh+s0v/A5JLPVEFBcUGTozPAjwFBoYQTfRIf8HIggT9R3bvf7LPzjdBDACGwAQ7xge9t/8MM8I+wm7BRgZAaz0DWQHHRz0FgQ6+NP5/g8R5gMv4/vgBwwtFuLl6yTW8gkULPT0K+4YGCjO3goK4AAQExjB4xL9CRHrE90Y/vkBIBwmyekU8OohAQEB5/sI8zccHQvtHgcw5RTgAA/++OHw7gUZBP0IKuwiAMj4Fc4X/z8SCxYZBggeBicc+jcSQ/Ew9KozCf1KCew6HQkSBP71P/XgDOcBLgv+JiABBRkNCgjz8+INABEg2CoeAfH/9g4sF9gd7TUiFe4q9/j9HQoaOvL/rjnyAer1xO/f+ekBFwFSFvkW9+UQEtPrIB70DeEdDOsF+Ob2GA/n8x8ABfwB8Dfw5DvyG/8T5bAI8PDqCgsK2ewqB/ctFe7t5fUKDBwhLQD9DPwsIhT78QD05hBDNw8DGuAjACEk9iMP9/zf+hYEIQ7IGBwnA/QUBRwNCfYMD/LiEB4t9f0g6e8f5vfhJAbjDdEvJ+3+OQ7XAPnYAQvnBhLSChUd+jIS5u4QC/YS+u8Q+AklGgETPxITEAYPEffsQt0kEP7yERYUA90n5Ajv3QpF3iH7A+TFCgYY/xATTw/CNtv9FQvWuScD9f4L9iQA1wwFFC0S4vUwKgMeDPDvAwYUAy3+J/vCHxH29zjuFfzQ7u0ZIyLg2AwO5gIJBRzp8/b5DersAvMuCA/1ECRH7i//Giks0Ag0EAD88MoXD+z08gkaF+7oE/8X8Rv5+v78Ku4ZMv3+H/j1HSEs6Q368ijgFSwIFSEPzQoOBxQMBuQSABQyBeovHRDj+PoYGeWtH94rLwzpIioc3A0DLh4G2STKOjQC7TwdBdYR8+g15izVK+wV6xTL3SwD2esJ8QkFBSCvIPX749ceBe/49fkvMNk6zvAEC+Xb6Q7s9gUAGSreNNv1CwLGvxb87Mr7DBb6/Qb7BRn23rUC+/T9EgUxIcbjBenzJP/m9QII1/8YOOgpAPwcF90UBR3/8vvhBA3V3Az8Re7b7QPo5AkS7CYhBR7/DA7sGQ/dCBD7+BcfA+8f1A0MFg4lMOH+8/DuChbaHr34JB4AJUD+/RgmBS8Q3ATdDUEW5iEjBvMiLCMLC9AAAy43BAgyHwz5CQ4tItDxXbcPC/3g5jrlQdsOwyHVRdgiviQNFOoHEg7KGejh8gXmKP5xHv4H6v/r6k8A+ejc/0fpVvnuxvnwDBj7BxfP7vAjAwb39kETQhYj6gkp4cbi+0Y02ALcA/732tQ2+yiBIyjuTCsV1Vf1BxgmZN8HRzQRBkkP8uBl0xVD0stG9gQBEDlNqAwEKBYOBQ3T5Mz16sz8/wrtDh8LKO0GKRXg9urSAUEL1Qva1h01DeoJuh80wNgS3sAWZxc4C6H2OAccQ8nb4ebOIyLRCNgkp9D1E7fPvNP2ZLbzHPYmMcAB7PEw7fjw8iDOQxvy+/3V/wHc//oC+wvoovzJBL0D+AgLI/YLsDroFese/xs1+AMy7Evm8kYhA/7wBirwEPEgBAgW7jEKMgglQuUg4eYYCwMZG+DMJQvo5hLcuwTZt9xCGBgE9T0Y9SXQ6Sv9GfH8FfkFEvskCAz3+7UoCP8M40Dv2N3E6xQCLA8oGu79Bvgv+PMU5wpECsX+BvUOI+kVNcEPCuUFGgn/HP/51TvkHCWuFi/Rx/Tv5iwY3AYE5OerCDX5wvfvKPfk5jzV1vrGDQBYAgLVGhgXvuPeAAUO7xYHBAsuFAYF9cX33OwA5jMwEVg1CuQ56TfNHv/n9eLkMCIYHxb8E+wH4Ogs/tDn8fQLKRfy2f398R3WFBISzAn+EFoQ/jrsBlASrAoOCfUU8zEj6v/k/PcW4+4j2v76C7bNSeTaKhf7IAbyFeomyATXEBQb5CA/B/3pL/IFUxkw8MkrIdAlOcHp9wDeIfcaEgewEfvxIP706gwmBg0nLRJS6fQbMOXZ+AUTGP8g+/APG+0pD+7tOQwGwR4Iz9ioEEL/3/DzvivT/LL/HQUIxBIHJuLcLPnY9gO6+TP84tYIQQ/yCynrP+0XmQEb2uDxI+8lE90p6SAZKMXjAu7g3ge4NwzWCBXQOyoe7fjXsc3YDTfjKd8VCD4o0yQg7PrUBvMPywTs+tgPJsvzFNHb7P/UEBAb8xXbEx8BzAQT1Nrp4QkDCPAGANkGGvQJ9v4rAdv/9SDHEE//ChpC+Q0v1hns1SYisyNqCe5APtITK/IsGd8Q9tISDhPaBBv89Q8JEyL2YAMD1TELAo8V9/Ad2/8N/gm+/tn0/gTwIAHW/BABLBPQBwnHR/fq0+78BeXagiIN77oN3REOEcDtDgXN2iQIEyn2Gxkx8v/X6P7m7PME/RzIDwQmQCTw2wsN/gjo0gvuCOwC1EZABBUICuMa6xwiABT82/goLFTyHc8I+9YVpTQMMSEPGx4LFeGxCUv/GPj++EMc8RX56AYX0x4UJu31BdshGSEG8QQnId7/NRcVMdoWQc4dBALuMxjMGwwGNCfZESb+y+vx+i0T1wTQJf86CcP5KNL/9y3w/Q0x7QXAMB0kRBC6J+j6uiHdCTAgwBbezz7/3SwB/bwV6AMY2wEMCibwBuP/2cTbUtNEJqwnNiUz/AT8TfnyDEf47ubQLMsjFWcUOzEM8QovEAPzCRjlExYV7EYqMs4YGzEIHQ80HSA43hoHDzgP+CAOExPxHSs/IOcM/BUT0xzZHfPnNqbaNRL4zQL7AAQE+vgPOBHcAR/5IOv9KOjy+TL5ET0nMBTRCfgBCPkE6RTp/v8DACIM9tk52g/d7wEYEsYN4O3/DAbZIATaH+D95E0RIzHg9nLprvYmy+Pl7/f5Kic0uxMY+f4S3K8WE87uABEZG0oCQdf1GxD44uTk++kMKCoSBgIXJ9EozQH2IfPiOxHxBSvFDR/4Gx5KF+8N+OgD+BgPGBv75wJVMzID2ek6y8UA6hgNF9X4V+0uqN0SldMnL6RJDicGDww17dnhNc4FCiDcJf7uAhIaKe41Dc7S/+j26hs+9vczAiPX5vwSCQcx3CIu5PceKD3i8+Py5sjs6kU06tw6H+YI3vXg9vW8QsH46yPupzoKquzwyrVFtFPsATLqvPQpAeXt4t+vIdsVIksxAakSCu3j7hDj1xfd5tRC50Hl1vQTBtob+uUY7vj2FQXT+QfWDBLFCRP2yzfzxPDy0gL0zkgUFyUbP+kG8tf//q8AC7AfPw0wBQnK2fHF6tyjNfTJH/0XLVQ0rSzWqwnIoScx0scYuQphBrpi6r77uvk0J+YxLfb17g/hBOaeLbX6uQQz0xfU+8/250PW7yrZBckW+doHBgm/CRHcCCbb9S4HFPq0CPkHuvQk/BfZzEo41g0b3zX6CPncLccAQgQlwqAgDhf1BPcP2g3SB/YiJdEC8w3gJurAKf4jF/MIGNP08xbQCu7iGR/z++T0HAAV8Nn6pCUmx04XK65Lu7EH9NYj3LdFCw8eIxvZWu3UQQStJ+Xt6QT2EgwUtevRNEMXzzzutSopBydKJZpHzuwuEc0e9tTfJQUY/TLYEu3V+g3CC/cO4TwM1eA6/O79ASYPtwcvPtzZ4vIHRfYS5vAO+Qf6B0v9FufzgQsXtC5IHhje2fHPBdHz/fPUDQXj7/ArOacf0i+n4drz8Tud2iXR09zMLQEbgd7h3vgitQ/80dUEzvEl9u+/2gEnAvLBC9vV+vAP6Qjj2vRD/iBX1avvyacLLdsO+dY5WgraJ9jZJBHgAtQCHMX2GTws1x0C+T3r7S7qvR0b6CkZRIc+zMpF4r1O4+g5DhUYNVXGRtwFC9PSIQUFBwgiAi0mxRHqyR/j9AL5ygsKrP07P48A3MYF2e/1KPQC+bDvGUfz5PMFE//K5wdHsQnXIpzq804SFAvoHsggU7QK9+HhACPiITGdAiyB8BgJ7s+nwb3w6eP8M+0H1x0r+7nh5yXmWsLm89cPTAMHLQShHBULBgUFxtQkDwXe+QcR5QcBOfbjEcXLScz0GBaxHvLuD04UoS7hlSXa2A/kyD+9zR8UHMZL/uU34egI2ctG4RIYDEarOfTTKM7zE+/TSzo0HEYw2O368wvX+SL94CovDekvFA4Z2c5NDe0Y7rkM5NXy/SfYIujjOAPssv3oMxbt4vEk/Lr55Rv52uIP2sn5DR3FDBKo+TI9EDb0yhea3QD06fUnrgQrQgUxK/gt88btvBjkBykvJVXfGecIYNPb6N79yS3n5DQB+i7xCjruKPANGRIfPuni6gD+ERX8G+X5DF8/6A7F2gW5zRQmzUgjwP8W96Eh178gzt0b9sssE//3VimUYtLkF+PzDu3mMfv/GVcxnRzmCkzTDTXjtgQkAjFEMdn51eoHv/0jAuUWPPgkGyC9+Obe/+jzGSkY1xfm6Qgu3gMf/Bj86dr/B+sH8wcgIYEl37Dx2tHeBgHqCfY32f0sFdUEJQPIz/1E/0fl3PQN/eEgMBXghLnqThDaC7rmC0m+JFkfPivp3S4Qq+UD3IElgfUr+Cn606sl5h3g+DPs+lHA6xXH3QIi6xns6iMwxTAq0Ad238MSDKYrFAEtPeq0H8HaT9zyE/G5KhfxKDUk4Sfn/1fp7TfjwTsq2D9WQsnp5tXbyt0/9tv3QAEcRQkC3vfaHvDyOh8A/Sj39f8au/LxFgQJ2A/+IwIC0AGlCPYP8QIm9QoDrwP2K+3TAgs+4tzz5rHKmwcH3hTa/NQdvLQXBQs54a/VAKjy4fLBGr7bADghQw6723Loyf+B3g8uDgklDa8ezL00LfLf9qT15P4nL/0i57noFiEantwI3hbhz8/22cEHBvYkLqXyMNXLDN2BP9+/A9TiRszA2SIm9zvq1xP13BTq1UUw6gwK8EW2+ulK4fD50+kUBND8Gkqy9PYDKvIUCvy1IvryEggmt2rbsQff+ScJD0Mb49YSFt+sEOIc7h4jMP7tE/31DAaHMtvkAcKz/i0GJtPm4bv8D/MVDDPn7LjpErnx2u+jDPD+vADsI8ve8H+l4MznhP35ENb5Pt/K7yPv+tSkC8VIrtvau0/0uP4NHOXu1MoRDCkA9OPNCwX0ByUv3B7q7AQLuxAeEzYC9yHcO8/q7Okc3uwU/T8A+Sr6Di2wXgr8/g88IhAC7/oUsADz3Acp/R4AHAMKjBs4BuTsKdG69vfzBbEk8BvtJQik1Twn2CnwNfcEB/Ap5gNHN9wm2/vv7e0BG9sN5SYD6xf9Efc3JC0J3gXt8eytDggA/QwGHudEF/r25efmBwA8z+E6HdUePSXNAeLu4gPrDEfi9A/GBhUA6xHh2QPcrun298/Z0A7b5QIG+wjUD6bizhIG+sbf6kDR9u4a9RIB5wAAFAg4EwYTAQ0SIBkZ7+ML//If/Rj7IfvpFxFKD98P6N7RC8YC0zIeGPn5K/ImBA3TzDkoEuH7LhMzErZDLSG2DwLzD+X1/hABLC4WEcvx4LsC6M0DCfoH2/AtLD7lARcFHMcfBv0/GhUZ/f8lG+IR4vENYgMSD93sABoCR9AGDgQuROcNF/MWAM8R5NgFB0v7LVfcG9u/D9/dLP7nCwMG6/4Mz+MU/tQS+PoBAATbHxIIMcDcyzMZvSP8JEjixA0e5iUH+PQvCPIRJt8p3PELJcYVOfb1JPb5+igU6Nzvy/zgEPEE3iISDST5EvvX4eb/HQ4C4fMRIs4fAhUST8TKIwb2/e3uGwnyDB3u4f3luwnPGuwB9uMI4fMWIkT+2DHv8bLP9uEUx/Tz6jr49ssAAxLt0+Pi7vbNFhk2NBMB6v0k+u7dLf7hM+i27xb9LfEAFsvpB/X63Be8ssC/ExQDGh72KAv5AS/p6fDn8/M61AkX2vP84+wFAA/R7/FYC9Md4Q2+8Pgx7a/zy3of6cIlC/cG4N/7LAvYEyM+EfQJGSbx0E/78hMV4v4pKiz14O43zRAjyfYfBA8GFhX8D7wNXM8T8/uyF0HXIzISQ1fa+j7Hx7v83dwpsRY46zoS0a4c39HB9QXrPuAB8eYgFwCoMMwWARcNAdX/MADo/vPr6DEECe/B+f4XEw/6BAr39JwMACMeEfLt1dkcB/QI//3/6hDiF/P2GwMM7QHA+gkI8zYY6kAWFRYDHPII7eEHLuQOIBXj7fch5w7z8PoPD/zvB/gOsgMFKBzW5eI3A/W7DDDpBfnq8Bvp5eztBg4BC+Q69yH2+/co9vQFDPwVNdIGJuMMA8roFwXoDiscH/vB5BT9CPK8+g0lBw4vNewn0gkV1wvf4QYDL+jf8+QHFNalAv4h5/UZ9CEaIBX5DNcArDCrQfYJCfIm4y01FwMFAqwQDfX64//kDycNAesOHxTX7AfHD+8FAtwUFdrF/R4V/w4P+1UrDxMOLADqxuwT+Cbw+rsNAgoIHz3l9PDR7Qos9uj32d7YG/29/BAT2d0TGlwWHvf2BCHs/wsyEP7w0SfLGSMD+hQFAzvU1SQlsPNS0csV9SL8PA4g/gU6DCv+7/o1OwgX8SviKAfyMOHx7bc25BEI1uklI8EHBzPWCALUExcXAwMCL9zHChI/2QzbrATzHyYy6QG0yQkFrv073d7uFQLf9CTmQQYsAP4QDNssC/sN9OjbBQQKG/DUCQG6/sgWJgjsG/zoDw7y1fnr5d4KDy3++AdEDAvhzRYP2g/FHwfm4/oWEtodHuYp4CsAAhMG4s/82Bga+/XtEfvdGwEEJNbTCg81y8sA9g3u4Csn+vXfQ7RHAfwBAf0v+CogDi4cAuMSNRPr+dDtAizj+Czb5SkjKvnn1iEU9QcnGCpK2P4fP/YDATfg/RtCEOHJDfMOMPcT7BhLoRXlHCIO/OEg1TQ9x+T+auMwCOorMr31Iu8zxea6+DKsxe/lHu7j57wPE/8RK/XAtej/5+f38OTh+hj8AOUM6vcVLNg+QugvDAowHw0DI/77+CkIBt0oLk3E0vINDuUOMvEOLdzS+bLFIxO/rRM2udfC297fgtI45zXl0gocKhb+4q4J6phhQsuB/e8aKQLNKKvezNHOO/EPPBYJ+8fn8PsVxh3vuugTJgb3+gUPABwIEgX3EyL8EwEVGhQDxc7i8xobCfQ13hEA5fsT+dH5EORNMfAFVvAVR8wiEP/C2mfTUGf0FyTNQTHg910SrdU62TFiEhZn1+IZ7MH35ODTBd8RKs03Fbz6QPXZ6P+6/A+yzujb5/HR9yD17RzaIuAx/OufvgrlxTK4FcHwNtXetBXc0/X2stj2GBa//CxsBPzgyfMWgfofA7/MvAwTGAaYH+PlGs26D/T87PYLLQDhzxfs+Sv7yPop++jq9yfhJ7MkBAToE/nbKSASEuoi0ggg6RAV+ujb6y04/wUP4cbyOqs5KtZGIdzdKOr5JOui0xvCVFP8H2zvBBH5/1D23O4F+zkAzycq4B9L7tN5Ccv1TOgjSOVOVdAkFREtGPXC1R/NDUIYMjvj+erepPPi/tYpA+wP3TI1/w8I7cLYBPbpJv7J4en8595LyfbQBNAv6r9oxsza89cEGCT19hMhPPwQDNjsLMbf+D+ItAnzCBEU1hsb7sen6/QtI0Yc4wUw9t6jCMr/jdwDJgrv+sQP+gMFH/2yrcIN1ThQuxf38vH3CMcU+PI5/+0GGujYCP6v/PDrIQIhH//z+RXv7UX87t0d1RJF7zMtFOA86dx/2eXpO/U+UMA1aNfuTNHnR+PM+kzxMkLgQ0jjOxXSOPniu9vm6AsM8zgW6tdB4/RG7uLlMuEIX/s8HN7S6Om5CwzisxoI48ulCivh+RMK0e7lX/wUCBLfgfXvCRETB0El40swySGB6smruT8w9L4cOvM9z+nPzt7O8oEnDePsIQgOzPTZ7eTqRK/S9yQp/g8ECP0V5Aru1wD6DgbZPuvcHt/d7zP3ABjQFh/hDzXsAx7huN314h1B3hZV7zU5J99V6u7kNNwmKO1JO+0vXdD6Z/PjyFL8LzbVOSfM8DDi+DUEp+dD0E06uCAK4CAiG8c+/MP3XNYn280xGtExHgcGIAP/9FvWGu7fzgXo9DHj+gv90/NbAubrIO8G9Uf+AvsPIFfZ4/AC5wLu/w7uQuvg9dU+JkgX/+/TMqs8OAYF/ggtOvyqyMr06eu9EAMI1hTtGs4r8SHRzcPm7Tr8ENTk6yPeBf8j9ww5DinWCkK85gIGzd4N6RcT6QBAyigz4xEv8bLX/M4gP9wwQ98OOP7uUgrYxTXONULuS0D990LaEULly8pW/jtD2Ecw7gMwzgdMysfrUM0uQtJxJ+EHEeOrQOzv4D/uI1H5PiTn1T7u3xT7F9ogLd4EPQcn6RQX49Mb4/24X+3uH/8yA+gHCwDSCAEtwkrtBCAH/zoAEBPWLQ/XzdTm+/IPKhAODRvAC+0xEigZ3TnRtx/e0FhEwfUADuEuGAVA5eEhorEYIhz2D/Dg3wCqPRQX3kMW0fI1wu0fBNzc/QT3A64t+tIlFc/qBeTX0hrgHTfqCAXY+iC08C3w0NRm5j480DkP4CQvzfpI8OvrW8JTUN87YdAI/sgdMPjU8zntORfsPSLoCs7uBAba9PIZEwn1/h0Y7u/31svxAhTPJPMU7wrqAwwdFfTxIwrk0yPDrMjR8C74Ne3xxQ7f/fTxMuTd+eoAD0D46PUk9R4i70HS0wYE2hEWw1OZ3g1pUJT/2s2HNCPLHcITpxMsIg0RN6zl08enIz/dw7L+6vXx5hYBBQXqzO5TH/fQD/616dL1DxcXQTLyLfIH/ja79vo+sDIQ0Bg0FjBKIxQh/wb0E/8YL9MiIAxNRPoSagDj8hv1ECHUKxr8Jiv/6xEVA+BT2yNB3kYg0cEF3tML3tTgchTs6R0uHffxHvC+49YomxbhEPjy9hsBB+T0KtDVFuEUGeGTzZTJDxAN/r0BBPLhGdLi5SzPt+jkC9YQHCVnhxQa0tgUw+/qCxREzBv37sr3IwoAxfcDJB0jQNb09QLi3R/z3srqEhQd8AnR9Sj02+cOFAYv/xQXBuYr1PoOA9rf/S/f/gUvHxTlKOYG9fXU/g344NgJ6QLpEe4KGAv85BFDCOQOBybyyhrsGQki6QMKIQ0R+vsh/rIf6SwpCPsHBhr1GhDsIfC4H+INQQT/IQoH/Ab6CQIGyyDkKj8U+ikKDxoZEQAg9wQK1RgrHy4fDeD+GQEQBfn9DvQpNw4FD/vk8voRDf3s8yPOLCAL/y468uIO4v877iwrH9RLABT06BcEBP3nGDU/80Td/PQZ6vsPBwrICP8tSdok4yD2HsnJBTEB+wjlGw78FMvwGg/2ABIM+tD78hjv9gf9ACAFv+sf/QqW7zQI6f0l7vDzHNX4FPsX6yYuFs8AEfMIBhPj2vIVB/D1HRjOAvsVLRP7GBQa/xEA9Qv53SPQHgYf/CcOKvEBARjzB5Eo1xweDO0dGwzoPOQcLQjFN+0pdgTzMAj79AcJDSDoyhjaGFUr8DwHDPUYEwoZ8q8r4UouLNke7xrt8gMaRBBE1ju3D94Pwq4yCBLf6egIEApk4WPtGMbzyy8Ap+UcHhDXD+US6ietngoG/Kft+hYd4hPJBQoCz90e/vHf8PMa9w4S6hYAIbzqLBYK1RrmIdoA8+8ICQvM5wbw+8vx9AvECOoYCQEl7BMdEP3z0Rnn9BTZLDL+Ewz5Cw0GB+L75PMa1ggZHQ4xF+kHDQztDO3OIeYZRx0AQTPmF/b0BRn7th3QGEwXAg8zC+MGCiEPCqH6+TNcHeYYFfkG+vkBSPrhL9UxLhXq+gQUAhQeFhwE/98q4ucCJc4ED/3bsw8wNsf+F9FOBx3sDQEY6wIOQxXb8A7X8wEZ7+8zCBX5ChMf7eEwu/kVBbndC/sC+B0kH+YNB9bzDBS04QgZBdoTAyjHBA3jChoT8AIHC/jfA/Ag+QHtCSHyFfT+FBYIBAj7B+wIyhU4DgcVBA8A+/sLMAL6DtYxFCHwKwzRGeYi91IHmQm7Ezwb/C4oAv765co/6d8L5icxGfQPDgAS9yX3FgUAEwQCNOzaFwL0/yDpIhghHy3SMxICAQMpBewCLfYH/AynPwEfA+LI+ewC09EYOTsuFSX06vgVyxwrDOIgAw0RG/UY5w0PB7DjD/T73ewbDDvdH+cIEQClsA4G5dsHIAwqARTY8P0QsNcU/w/8CicHyvz0CDYQDecoEuQIEAQ7Cdsy5QMNAwoHECELB+njG/sNG+xKJgD7EwIUCwIKFhUF9yPvIRwS8BIMA/f3vAon6ekv8idaDwkNEvUC7e/3D/HxFds3/yvzJSH1CRX7/f/SzOMAGVsRChErE/s13h8HINEm7Ts3Au8bFvERFBf3HO0BwlLMH+L2yfHUEt36/yIzHeUspgjv79vnG/wI1f8TDQnjDtwAAebG9CP74NoK/UEi+j75P+QLzf8D8RHP/PgcJRAj9+QVCOi6BSD96hT4Ecf+D/0uHQLrKRgFB+AKFgu4GfYHPiPx/SktCfXd+ysNrCjnEkcoBP4LK/n+CPQiCLww7gxDCfkpFvsC5ugCEAro+e8fECYLFCbh8RHzAxkHpAbaKBkk7SQBANI13RlJ8eIG0Q1E9+Ue/xMEDxX0RwPmLtgmIgHgJvoB8vcb8CXdGtA+0RnpF8L0ZgMO7Rr1JiToSbMv6QPi5C4P5cT87SMr9iXI3ggH0ckdDQXf//YxR+Yq9PEBAtO0Agfh/OjYCPDeG/Qt5BDO0BYK5OsUBQ3gB98DDRX5+gwkGNTK8QwL0gv3+hT66wYrGfoG4PYRDrnuAhEuEuYDFv0FH/0HHBDHJukb6ybwIBXy9AkREAvt2QXpGg766BIR4fci6Oj/++EoshMZEgIlHhgBJSoGKhjEAtkAWQDxCiEFA/cpDf0R6TTsDUjw8wsVFOj0Bw4A8HyBgT1/WO85GcfHGusEBg/OXlK34dcgv4FsaYESgUaBTwIpUMNigWO0QyvV1rnaz6g4O87gtRnLhmFniECTSJIN0tIw5CwODsRFQr/b4gvMM87qQTRM+Tc0EuYhMinRG4QzTKESimWnZLPGOT5W9EpR8OosH1qxQJE8T7/WneKg";
static const float vn_es[VN_H1]={0.002770619f,0.00212067f,0.002942525f,0.003965846f,0.00325691f,0.002154651f,0.002709959f,0.003172836f,0.002224091f,0.002915138f,0.003071204f,0.002515724f,0.003064982f,0.002524096f,0.002978739f,0.002681458f},vn_fs[VN_H1]={0.002770866f,0.00231627f,0.003386216f,0.003666426f,0.002904381f,0.003410213f,0.002970707f,0.003774137f,0.002530253f,0.002667035f,0.003711204f,0.002263069f,0.003057671f,0.003196966f,0.002794181f,0.003563996f},vn_w2s[VN_H2]={0.004994635f,0.007294242f,0.00714618f,0.006233622f,0.003797131f,0.005896998f,0.004631607f,0.006475136f},vn_sc[2][VN_H1]={{-0.04735576f,0.4575865f,0.08622818f,-0.02263413f,-0.04745544f,-0.7386846f,-0.07324208f,0.1170214f,0.2461265f,-0.2163579f,0.2240351f,0.5493248f,0.1421613f,0.04115794f,-0.05420406f,-0.09373517f},{-0.6198477f,-0.514686f,-0.04475664f,-0.4894416f,0.801212f,-0.08560622f,0.575525f,-0.08610682f,0.01067981f,-0.2563864f,0.3789841f,0.2403875f,-0.3961339f,-0.1314769f,0.2914196f,0.08581095f}},vn_b2[VN_H2]={0.1738791f,0.1474488f,-0.07666812f,0.104841f,-0.2220177f,-0.1658038f,-0.01914391f,-0.004559087f},vn_w3[VN_H2]={-0.764697f,0.9088314f,0.9209622f,-0.6457433f,-0.3873365f,-0.6429221f,-0.6218132f,-0.8162875f};
static const float vn_b3=0.3116964f;
static float vn_E[455][VN_H1],vn_F[455][VN_H1],vn_W2[VN_H1][VN_H2];
static int vn_b64(char c){return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:63;}
static void vn_init(void){static int done;if(done)return;done=1;static int8_t raw[455*VN_H1*2+VN_H1*VN_H2];unsigned acc=0;int bits=0,n=0;
for(int i=0;vn_blob[i]&&vn_blob[i]!='=';i++){acc=(acc<<6)|(unsigned)vn_b64(vn_blob[i]);bits+=6;if(bits>=8){bits-=8;raw[n++]=(int8_t)((acc>>bits)&255);}}
for(int r=0;r<455;r++)for(int h=0;h<VN_H1;h++){vn_E[r][h]=raw[r*VN_H1+h]*vn_es[h];vn_F[r][h]=raw[455*VN_H1+r*VN_H1+h]*vn_fs[h];}
for(int h=0;h<VN_H1;h++)for(int o=0;o<VN_H2;o++)vn_W2[h][o]=raw[2*455*VN_H1+h*VN_H2+o]*vn_w2s[o];}
static int vn_correction(const State *s){float h[VN_H1];float rm=s->remaining/150.f;
for(int k=0;k<VN_H1;k++)h[k]=vn_sc[0][k]*s->side+vn_sc[1][k]*rm;
for(int i=0;i<UNITS;i++){const Unit*u=&s->u[i];if(!u->hp)continue;int c=u->owner==2?4:u->owner*2+(1-u->leader);int r=c*91+u->pos;float hv=u->hp*0.1f;
const float*e=vn_E[r],*f=vn_F[r];for(int k=0;k<VN_H1;k++)h[k]+=e[k]+hv*f[k];}
for(int k=0;k<VN_H1;k++)h[k]=h[k]<0?0:h[k]>1?1:h[k];
float o2[VN_H2];for(int o=0;o<VN_H2;o++)o2[o]=vn_b2[o];
for(int k=0;k<VN_H1;k++){float x=h[k];if(x==0)continue;for(int o=0;o<VN_H2;o++)o2[o]+=x*vn_W2[k][o];}
float out=vn_b3;for(int o=0;o<VN_H2;o++){float x=o2[o]<0?0:o2[o]>1?1:o2[o];out+=x*vn_w3[o];}
return (int)(300.0f*tanhf(out));}
static int evaluate_live(const State *s) {
uint64_t key = VN_ENABLE ? s->key : (s->key ^ s->metadata);
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
if(VN_ENABLE){vn_init();value+=vn_correction(s);}
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
#define NN_ENABLE 0
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
static const char nn_blob[]="";
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
