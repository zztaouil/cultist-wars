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
static const float vn_es[VN_H1]={0.002008063f,0.002044966f,0.002176512f,0.002016484f,0.002354646f,0.00220815f,0.001761616f,0.001706606f,0.002248262f,0.001871753f,0.002171148f,0.002078359f,0.002526675f,0.00205396f,0.002049487f,0.002091209f,0.001975175f,0.002083051f,0.002098489f,0.002189463f,0.002292169f,0.002109145f,0.001855022f,0.00187142f,0.001803015f,0.001851587f,0.001743143f,0.00189713f,0.002701251f,0.00169556f,0.002079479f,0.002731496f},vn_fs[VN_H1]={0.002117825f,0.001951205f,0.002129589f,0.002168303f,0.001855428f,0.002071197f,0.001810557f,0.001624889f,0.001961235f,0.002509684f,0.00217361f,0.00178954f,0.002089117f,0.002286848f,0.002143349f,0.001810974f,0.00167938f,0.002454549f,0.002234108f,0.002172573f,0.001905824f,0.0024523f,0.001645962f,0.001824026f,0.002295468f,0.001804162f,0.002007358f,0.002080749f,0.002189196f,0.001648466f,0.001957099f,0.002538883f},vn_w2s[VN_H2]={0.003447475f,0.002988553f,0.002150769f,0.002621646f,0.002536113f,0.002141197f,0.003219504f,0.002474376f,0.002683475f,0.003345041f,0.002730561f,0.002771034f,0.002443457f,0.003221959f,0.003045781f,0.002634508f},vn_sc[2][VN_H1]={{-0.02620999f,-0.5069027f,0.03677277f,-0.0365253f,-0.8316461f,-0.2490386f,0.6101149f,0.612239f,0.03710496f,-0.1706938f,-0.05760727f,-0.1367544f,-0.856622f,0.3367332f,0.2305205f,-0.8183141f,-0.3863326f,0.290025f,-0.1694488f,0.05873939f,-0.4800354f,0.1672511f,0.5699702f,-0.01320276f,-0.6023824f,-0.2393422f,0.3676718f,0.1511078f,-0.03445372f,-0.06131923f,-0.1380215f,-0.008928838f},{0.2129002f,0.6044026f,0.5679809f,0.005657289f,-0.5039756f,0.08606063f,0.0354182f,-0.4914469f,0.1880346f,0.2582671f,-0.05464571f,0.4284501f,0.2976521f,-0.206933f,-0.2856675f,0.2305943f,0.5185081f,-0.05840763f,-0.3936255f,0.1037544f,-0.2528413f,0.0269838f,-0.2481668f,-0.2688656f,-0.6649246f,0.3278858f,-0.0886957f,0.2806596f,-0.1015082f,-0.01098829f,0.1691794f,-0.6122302f}},vn_b2[VN_H2]={-0.1231965f,0.1151514f,0.04590524f,-0.2268021f,-0.2863298f,-0.07929893f,0.07068341f,-0.093629f,-0.2302307f,0.06458732f,-0.0006945727f,-0.1760859f,-0.2085576f,0.02530886f,0.046642f,-0.3001917f},vn_w3[VN_H2]={-0.274726f,-0.1553269f,0.1627747f,0.1986311f,-0.2004586f,0.1974218f,0.1569656f,-0.1245589f,0.2637287f,0.1454407f,0.2096248f,-0.2328758f,-0.2321519f,0.1428097f,0.1553628f,-0.2167214f};
static const float vn_b3=0.01178914f;
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
return (int)(500.0f*tanhf(out));}
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
