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
#ifndef NEURAL_VALUE
#define NEURAL_VALUE 1
#endif
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
// A neutral beside the enemy leader cannot be counted as secure material.
if(RECAPTURE_DISCOUNT && d[1-p]==0) access[p]/=2;
}
value+=access[0]-access[1];
if(RACE_WEIGHT && d[0]!=d[1]) {
int winner=d[0]<d[1]?0:1;
if(d[winner]<CELLS)value+=(winner?-1:1)*RACE_WEIGHT/(d[winner]+2);
}
}
return value;
}
static const char value_blob[]=
"WCYbqssr+SdvqdKkiqguKJelbKYGLKUrCSRLKpymApukJ9Ed5yYTrGckNqQ+J+spBCVeLLgsfqI/KgwqeifoqTWrnShGpFmndqKIK8sskySFHPSp96M0qi0r"
"GSjTH1+r7qY0rAqgIasVICgqtytKH+anjCnJmsAmvSxtKPsqdCgBqNgm1RlZqGWcahq0JfCThJ7ILF0rY6kmLMUqGaz5mpopoKl+q5SrkCZ2rJStByG3qHOh"
"kSjhKtApkygQI+4uwCPSpS4o+iSWqR2qm7CjMT8bUpNOnB+cCKrMHTAo3BiFJtWiG6Y1LBClVJj1HhQsSqx2qqirZqEupeIohKpZJDepZ6nWpk6e+ygcq20o"
"/qhiqtYioZ87o9ed3ShyqhIvpKUoqxuYz6R6pHWrDpsoK9QfnSVWnGuqDqdRKl+w3C+0KTelmCXfnYQm4qh9Gp4t5KlBKHwg16vALGYkHagkKBoroapULIMm"
"5iwqqKytoS+4JHOf3ql7I/om1yozqoApnigcq7kr/KM0K14r9KwXK44oqinNqUMmWStyp1iwpCsCpnmd9KfsJM6nkit0pu0qSqjCqZaoTyjaKcykGyMnLC2m"
"vCiWqTeqbCx2KwchBCzrlUakaqSQJ5qogKJoKMceLKuuFEaoj6Ahq2GpTCBDppugoig0oUKmgqWDKYMsDBkgqdepDaK+LEUqbStKIPIrYy1BLGihiqgFqLEn"
"S6hyqV6sTSpSosCm5imLol+qDaSIpqKhIiYWKdYWXioGJVYs0Kl0K4cpnSoLpDcne6RYJAIfBCrbIg8sniy/LOiq1qgvLtEkei0GKccqSCqDHDAsvZldKaUr"
"IKzuCl4rEil8n+KaJBzTIHkqDaoqIKCmKCW6I6IskqWop9Sn3qvgL5AlsyGgqsak9yjYrCuksicwJJEgw6FCpx0keiyerV4hj6dspH4ppiFgJ/okZLDxLVAq"
"Ryj5Hq8oMqcdK+OmC537mgOoHil/KuMsrihorhcuuyWSqjWrAip/qhAs9a6sLhqRlioHLHGoGR9/LJis2S0Kq58lUaxZrK6muCchrJItbq14pCMpQ6pDKl2s"
"eIkMKdehNZ8CqRmpkij6JXqt2Syno80rAqtHpxwlj6FvqYoreSmZKSiihqIRptmbdK1DKZGrz6iDpQ0o5R3Eo+2r7ykEqJOeUKqnJQmhc6YErfUoWipOJIiZ"
"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"
"F6neqCQkyak4KPMayZ2epRaoYiAwLVQtFSAeqbGPfqpnKHiklypULOUolSuvppwndiiXLHUp6CUFqPSdkKZpLPIsfi35KM4ssKQqG9CesCtrJmspZ6ggpuor"
"Bi3pIUKj9Jlmm7ckWCgOJaKekyMtGFkrDaZ4G+8lcazFLe0I0yoyLtEjCiORLIcpb5/KHmos+i1gsHKopSmnHeoirSsqLXoouSTSKROhfKU2pgyo/io5K6mk"
"6p5sp3EtgSmCKGseDx7oqictIyhmJ/+omSI8njQtIKjFq20oHywXK20s2isHL3Ek6p8ipv+dPSnrLKchUS/KrX0eVyVnpqyqcivCJgowhqgyI/8qXZ9QJBOs"
"3yf6Kf2uVp+1Ft4i2KgcjZAlfyqPrE2WBSrjLNmmCaLrkygw/KyeKAarL6fVJV6ku6gIIpWuOid9m/GkbqSTJLarbjA5qkakF6gBJsqoFyVDLdktgrA/Jlcm"
"WpiMqpCoPqRPHd0oFajyocmn8iUCqr2kmR5kK/2lihVKqeOjnSozKKgqM6lsmsAe5ScipTuomJLoKPwsJzBqMJYsG7DgLpUkBpY9rPok+SiqMEssKCWVqhEq"
"eSyUFW6sVSrTpbYm2C9nqEYf5iYSMHchRi9rsDAuMCCrJwutjS92q+oueqNnLIqVNCibqXcv4aoILCgoDiyDrVokqiiyLFepQa5MrBsscianKkqoDiYRnm0w"
"qynRnKmsz6wEK+ij1KAFKnUhCZ1TK+MmOx/uLMCbUZ4rK5YrfCk4C/wlMBvIqxohxiHBLG6rTS1aLnop8yh9I3AsAC9FslSRXKSdpd6q56HTIoKs7aW7KGiU"
"/KQvKjiwfy73LKOt7i1epecg+CLULEQtTC0TJNcroycBJ1wmhaxVKqkqSK4MLVwfRinNLLWaDyihKamopCpcKeQmhSlrK4cs3qZZqhstbym6qsemIax+mfYr"
"NKxUprYrUi1IqsWvO6mBJjSnrKv2q8QmaiG5qkKsuy5lrUWZa6tWq2+s8xnvrecrSa2WGeYngxqwpf2jrKhEqsEohySPq3SpPhppLg2wm6V+pnypUJMhDUKr"
"9alcqhsUxaTApt+nR69lqwWwZhkvKhKrDRngJ9cha6ocoZAkChowpDogiycnH88jQ6X4pDAm2ScfpyKkBxeOGlmkPiUzpmeh/CEwJH+d3KujJpUdxR0zLUMs"
"ACn7H40qOi2Gp8qYky7PIf2kUilkLO8oCyoqKZUgEiWVLqGmEqN2JecqNCvEpjequKaoq44sei4KLTkZ168irXEwKzMmLM0s7y71LeEw67VdKQYSqy3mLv0t"
"PSymrbWzfioLMS8vyLFBJM4n4SnvL+K0bbOxslYxwTFQoectCDKSMfSwizKcMsO1+SzzpI40NqjJNE8wUa8Qp5qxGi6zNBK0jTA4rZUw+DAir62sBa8fL8ot"
"mas/sG6ta518KkcuH6giKP0wWKj5qL+hE6caJNmrbzShpwKwxrA5smQxLDS+s0muWa/fM7muma42LU2xWzTtMtEzbDL6IWi1SzPVMEGwTq8lprAwz7RUMaes"
"EDSosScxNC51IhW1YCmxp4ItXbUMMD2pYjWFo+IxOLAPtBc1sCzFMUowjK4otBEyETF6rUEucS9irsK0MTEiq58jfLHAMJksqTBssEOx8q5EMvu02TACs8gg"
"3SR0Kuykl7XFMdYwvzE9MAMwqbGiqWYzZrB/slqsgKRftCSmczHsKtIrHzOLMsEvBrJOrrwyWTUrtLqu6SrJNH0xfaUbrJmyqjPNL7OlV6/HJ/gt3TCwrTuw"
"sa95LdmrFqknLoStaDQ8MBqhRbDPLMauGqiFLNqjs7Jzr6emizJcMeqd8ByLsGA076iBKUIrCy+gnFGw4rFhKMEzt7E1LcE19DGZKzGvjjN/sVGo4Cx0NUAx"
"oS3krDoy06I6MjKqYS/4MK8yxDOIoxC0ga/aJhcyOzQCIQwlvqmTL+geuqibq2Ex1yHGHoQwDK1Xm1uw8C0wsDstQ6kHKFOuAanMrNqwyrBNGDKxCKu4LTyv"
"5Co3JqGsxaovK/gnhCtXscymHzapML6sgq+PLweyMDLUsag1GyFJMCuwYSquppsz0bRMMPyZHKkLGq2uILFcqYWgF61kMHKwW6x4MUgxwqXxsJEqsiiOMniz"
"zKyBrSIoz7CENCEyAamely81iqsMMk+z1ScTmKsqH7DitF2xMLSlMOIuySkDMqcoKKiXqzAyBTKXslyudqSmMveysTTdLKQs9bTBHZGwVTTCs5AwWLBrJRWx"
"tC9+r8W10i8AMegzkilLrvghUjE8qU+sw6RnMGow1izBqhqwILSXq5e0Zi0Gq2w1IqxFLmSrLjV8LROkXSqzLHMyCSJpM1Cxwq36sogoYa4dKqUwcC3RKKOx"
"wyy+Lii1Ep2JLlgz4aTDHlKxcjG6s9ms9y6wM7q1Yy/grX81Has/nKstrrBdHHQyH6AOLhSqvayILLiq7CzPn9+unSCqNbkwrTBzsuIw47EjMueoiC30MGyi"
"hLASNVaxLi4vsFwwqjIyISYx+60ms2iwszF0M1g0zKrpLg0ueC8rqbwst6ufroAuDTLTjhI1ebGarpCnwqyZMXKqgakKKtG0ZDQbsJmvPqXntH8vVjKxLNUx"
"rTA5tdapBa6DML2r664Br7EWByc7sLoxqjFHLz6td6TjqSqsBjTLtX+yTrRZtey1srYPNOewxjPMNSC17jQCtUM157RtJg=="
;
// Full-state value network, feature contract v2: 239 -> 32 -> 16 -> 1.
// Output is side-to-move-relative, bounded strictly below terminal scores.
static constexpr int VALUE_FEATURES=239, VALUE_PARAMS=8225;
static float value_params[VALUE_PARAMS];
static int value_b64(unsigned char c) {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:63;
}
static void value_init() {
    static bool ready=false;if(ready)return;
    unsigned acc=0;int bits=0,n=0;unsigned char raw[VALUE_PARAMS*2];
    for(int i=0;value_blob[i]&&value_blob[i]!='=';++i) {
        acc=(acc<<6)|(unsigned)value_b64(value_blob[i]);bits+=6;
        if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}
    }
    for(int i=0;i<VALUE_PARAMS;++i) {
        unsigned h=raw[2*i]|((unsigned)raw[2*i+1]<<8),e=(h>>10)&31,m=h&1023;
        if(!e)value_params[i]=(h&32768?-1.f:1.f)*m*(1.f/16777216.f);
        else {unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(value_params+i,&u,4);}
    }
    ready=true;
}
static void value_features(const State *s,float *x) {
    memset(x,0,VALUE_FEATURES*sizeof(float));
    for(int p=0;p<CELLS;++p)x[p]=(float)wall[p];
    for(int i=0;i<UNITS;++i) {
        Unit u=s->u[i];int k=91+8*i;
        x[k+7]=(s->neutral_mask>>i)&1;
        if(!u.hp)continue;
        x[k]=(u.pos%W)/12.f;x[k+1]=(u.pos/W)/6.f;x[k+2]=u.hp/10.f;
        x[k+3]=u.owner==s->side;x[k+4]=u.owner==1-s->side;
        x[k+5]=u.owner==2;x[k+6]=u.leader;
        if(u.owner==s->side)x[206]+=1.f/14;
        if(u.owner==1-s->side)x[207]+=1.f/14;
    }
    x[203]=s->remaining/150.f;x[204]=s->side;x[205]=s->rng_known;
    for(int b=0;b<31;++b)x[208+b]=s->rng_known?((s->rng>>b)&1):0;
}
static float value_forward(const float *x) {
    value_init();float h[32],z[16];
    for(int i=0;i<32;++i) {
        float v=value_params[7648+i];
        for(int j=0;j<239;++j)v+=value_params[239*i+j]*x[j];
        h[i]=v>0?v:0;
    }
    for(int i=0;i<16;++i) {
        float v=value_params[8192+i];
        for(int j=0;j<32;++j)v+=value_params[7680+32*i+j]*h[j];
        z[i]=v>0?v:0;
    }
    float v=value_params[8224];for(int i=0;i<16;++i)v+=value_params[8208+i]*z[i];
    return tanhf(v);
}
static int neural_value(const State *s) {
    // Side, remaining actions, RNG and neutral list all affect the learned value.
    uint64_t key=s->key;EvalEntry *entry=&eval_tt[key&(TT_SIZE-1)];
    if(entry->valid && entry->key==key)return entry->value;
    float x[VALUE_FEATURES];value_features(s,x);
    int value=(int)lroundf(10000.f*value_forward(x));
    *entry=EvalEntry{key,value,1};return value;
}
static int evaluate_live(const State *s) {
if(NEURAL_VALUE)return neural_value(s);
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
#ifndef NN_ENABLE
#define NN_ENABLE 1
#endif
static const State *nn_root;
static Action nn_actions[ACTIONS];static int nn_ranks[ACTIONS],nn_count;
static void nn_begin(const State *s){
nn_root=0;nn_count=0;if(!NN_ENABLE)return;
nn_count=generate(s,nn_actions);float scores[ACTIONS];
for(int i=0;i<nn_count;i++) {
    scores[i]=-INF;
    if(now_ns()>=deadline)continue;
    State child=apply(s,nn_actions[i]);scores[i]=(float)-evaluate(&child);
}
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
int selective = !NEURAL_VALUE && use_futility && depth == 1 && beta == alpha + 1
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
