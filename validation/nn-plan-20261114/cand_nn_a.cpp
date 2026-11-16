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
static const float vn_es[VN_H1]={0.002865391f,0.002352839f,0.002375926f,0.002531352f,0.002126334f,0.002600381f,0.001763432f,0.002262905f,0.002982314f,0.00283247f,0.002456167f,0.002432329f,0.002159123f,0.002993691f,0.002186867f,0.00231138f,0.002946893f,0.002104477f,0.002986615f,0.002984565f,0.002522397f,0.002642299f,0.001670975f,0.00253602f,0.003467556f,0.002252237f,0.002194684f,0.002887209f,0.002412438f,0.003516202f,0.003340587f,0.002750615f},vn_fs[VN_H1]={0.002898924f,0.002352442f,0.002518744f,0.002475502f,0.00242171f,0.002800345f,0.001892495f,0.00195423f,0.002410592f,0.002636654f,0.002598043f,0.002204874f,0.002466814f,0.00296231f,0.002140129f,0.002425478f,0.003348534f,0.002787083f,0.00299237f,0.003290293f,0.002616808f,0.002943109f,0.00195594f,0.002404589f,0.003208464f,0.002267892f,0.002643635f,0.003241937f,0.002315707f,0.003102971f,0.003226784f,0.002820035f},vn_w2s[VN_H2]={0.004118947f,0.004712941f,0.002694267f,0.003183412f,0.003360459f,0.003100147f,0.003151103f,0.003910507f,0.003139748f,0.003107281f,0.003457647f,0.004475473f,0.003593451f,0.003225578f,0.003281068f,0.004052365f},vn_sc[2][VN_H1]={{0.15441f,-0.1140588f,0.01368102f,-0.02294903f,-0.7895772f,-0.1352525f,0.5378096f,0.6177169f,0.08736753f,-0.08292398f,-0.09487209f,-0.01531271f,-0.4887113f,0.1230801f,0.07061213f,-0.7813734f,-0.1371578f,0.1052518f,-0.1020578f,0.02732072f,-0.3052424f,0.03578627f,0.4868698f,-0.08510969f,-0.06516995f,-0.0933305f,0.1357032f,0.01661339f,0.005498627f,-0.06941972f,-0.06872125f,0.007101427f},{0.5163793f,0.7926776f,0.7571371f,0.03117579f,-0.4977377f,0.05141851f,0.001045358f,-0.6186079f,0.155536f,0.2993048f,0.03731799f,0.4949147f,0.2766978f,-0.1923656f,-0.3225666f,0.2470354f,0.4784175f,-0.01139917f,-0.606582f,0.1240961f,-0.2235594f,0.06735753f,-0.215695f,-0.1387639f,-0.5828516f,0.3739501f,-0.129316f,0.2216f,-0.1162165f,-0.1142004f,0.1972401f,-0.488894f}},vn_b2[VN_H2]={-0.1142221f,0.127058f,0.01526307f,-0.1795001f,-0.1801598f,-0.1176535f,0.08028811f,-0.08311202f,-0.1995349f,0.05527223f,-0.05348126f,-0.183937f,-0.1576395f,-0.01361608f,0.1742079f,-0.2360677f},vn_w3[VN_H2]={-0.5219154f,-0.4035207f,0.3359697f,0.3939943f,0.4292944f,0.391687f,0.3202207f,-0.3589957f,0.4744024f,0.2573585f,0.4082548f,-0.5056648f,-0.4551763f,0.3180718f,0.3008746f,-0.4549682f};
static const float vn_b3=-0.08124135f;
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
