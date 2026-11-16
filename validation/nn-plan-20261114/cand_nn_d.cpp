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
static const char vn_blob[]="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";
static const float vn_es[VN_H1]={0.003449535f,0.002293818f,0.003174903f,0.003123523f,0.00327251f,0.002544458f,0.002634133f,0.004141596f,0.002976584f,0.00319865f,0.003560582f,0.002413549f,0.002855579f,0.003637688f,0.002865191f,0.0042874f},vn_fs[VN_H1]={0.003107688f,0.002595853f,0.003870868f,0.00343083f,0.004404684f,0.003395564f,0.002650667f,0.003573206f,0.002791459f,0.002748706f,0.003592838f,0.003028999f,0.003432345f,0.002961694f,0.00361508f,0.003846766f},vn_w2s[VN_H2]={0.006920891f,0.007917141f,0.008109616f,0.006301619f,0.002808014f,0.007326895f,0.002994044f,0.005078196f},vn_sc[2][VN_H1]={{-0.03443162f,0.3401496f,0.001273984f,-0.06057016f,0.06399996f,-1.037245f,-0.04535149f,0.08140022f,0.3360452f,-0.2580574f,0.1030891f,0.3727653f,0.03897633f,0.00363421f,-0.07707334f,-0.04186217f},{-0.5890529f,-0.4609683f,-0.01572314f,-0.4262463f,0.7023455f,-0.2050508f,0.4599164f,-0.04814066f,0.1000031f,-0.351669f,0.3640251f,0.2608894f,-0.5291625f,-0.03630795f,0.5457666f,0.0438394f}},vn_b2[VN_H2]={0.1593664f,0.1335393f,-0.08988379f,-0.04378027f,-0.2312545f,-0.1929701f,0.2302717f,0.2909641f},vn_w3[VN_H2]={-1.017458f,0.9497119f,0.9920935f,-0.7659526f,-0.2796052f,-0.8189425f,-0.4933346f,0.9398593f};
static const float vn_b3=-0.07833501f;
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
