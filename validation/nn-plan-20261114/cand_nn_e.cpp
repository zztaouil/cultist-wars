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

#define VN_H1 32
#define VN_H2 16
static const char vn_blob[]="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";
static const float vn_es[VN_H1]={0.003421556f,0.001838111f,0.002492795f,0.003652467f,0.002559087f,0.003126132f,0.002599699f,0.00348604f,0.002214862f,0.003807239f,0.003439325f,0.002616839f,0.003349395f,0.002873544f,0.002624997f,0.00242463f,0.002904889f,0.002965209f,0.002824361f,0.00292899f,0.00252535f,0.003360545f,0.002371447f,0.003618702f,0.002984209f,0.002703858f,0.002217637f,0.003428815f,0.00271582f,0.003424663f,0.003062966f,0.004355513f},vn_fs[VN_H1]={0.003582845f,0.002312503f,0.003093418f,0.00390157f,0.002894597f,0.002925481f,0.002111223f,0.002551496f,0.003514786f,0.00301284f,0.003754559f,0.003329528f,0.003197028f,0.004096776f,0.003062737f,0.002712715f,0.003898235f,0.002943987f,0.003122783f,0.003142308f,0.002689347f,0.003321374f,0.00251632f,0.002904413f,0.003450574f,0.002774302f,0.002537023f,0.003764598f,0.003191365f,0.002900828f,0.003805429f,0.004122453f},vn_w2s[VN_H2]={0.004610628f,0.005165235f,0.003584038f,0.004452112f,0.003722703f,0.004481327f,0.004241131f,0.004679854f,0.004059558f,0.003819264f,0.00407584f,0.005017142f,0.004663933f,0.00487584f,0.00351628f,0.004699349f},vn_sc[2][VN_H1]={{0.05572887f,-0.384231f,0.04108362f,-0.05490805f,-0.9521844f,-0.09437675f,0.4655553f,0.2145147f,0.04462479f,-0.06713018f,0.05821675f,-0.01802385f,-0.3831378f,0.05186961f,0.06424153f,-0.8686408f,-0.1391336f,-0.02389182f,-0.02755605f,-0.01921403f,-0.2725867f,0.02904973f,0.397225f,0.02371008f,-0.05787831f,-0.122204f,0.09151899f,-0.03967362f,-0.05192345f,-0.05517419f,0.02650548f,-0.005876398f},{0.1543734f,0.5214882f,0.920378f,0.06699436f,-0.5482625f,0.03781055f,-0.007592534f,-0.7069905f,0.1077619f,0.3303854f,0.4701618f,0.3244916f,0.1969808f,-0.2456151f,-0.3106235f,0.1931569f,0.405775f,0.2344101f,-0.4128895f,0.09408892f,-0.2630382f,0.02004121f,-0.2577104f,-0.2895934f,-0.6327848f,0.2680122f,-0.1648551f,0.2765677f,-0.06264069f,-0.06428608f,0.1885939f,-0.5776652f}},vn_b2[VN_H2]={-0.1598787f,-0.003340071f,0.006694323f,-0.2032528f,-0.2150488f,-0.1415004f,0.01015039f,-0.1504518f,-0.2108261f,0.07414528f,-0.1170217f,-0.2275027f,-0.2225158f,-0.06391384f,0.2517519f,-0.2844019f},vn_w3[VN_H2]={0.6996548f,-0.6659619f,0.548484f,0.6168764f,0.6189379f,0.7203469f,0.5170067f,-0.5767051f,0.7287408f,0.4581301f,0.6814165f,-0.7750088f,-0.7321035f,0.5203812f,0.406862f,-0.7083564f};
static const float vn_b3=-0.1886871f;
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
