// Cultist Wars bot (CodinGame). One file: exact simulator, alpha-beta search, evaluation.
// Local-only code sits in `#ifdef LOCAL` blocks and is stripped by tools/pack.py.
// Environment knobs for local runs: CW_NODES (fixed nodes per move), CW_MS, CW_FIRST_MS.
#if defined(__x86_64__)
#pragma GCC optimize("O3,inline,omit-frame-pointer,unroll-loops")
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")
#include <immintrin.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace std;

typedef unsigned long long U64;
typedef __uint128_t U128;
typedef uint16_t Move;

enum { W = 13, H = 7, NC = 91, NU = 14, MAXTURN = 150, RANGE = 6 };
enum { WAIT = 0, MOVE = 1, SHOOT = 2, CONVERT = 3 };
static const Move NOMOVE = 0xFFFF;

// ---- search / eval parameters (override with -D) --------------------------------------
#ifndef MS_TURN
#define MS_TURN 44
#endif
#ifndef MS_FIRST
#define MS_FIRST 850
#endif
#ifndef TM_PCT       // no new iteration once this share of the turn's budget is used
#define TM_PCT 60
#endif
#ifndef TT_BITS
#define TT_BITS 19
#endif
#ifdef CGBENCH        // benchmark build: these switches become run-time so one process can time every setting
static int gTtEv = 1, gStaged = 1, gThreat = 0, gSimd = 1, gQtt = 1;
static unsigned long long gTtMask = (1ULL << TT_BITS) - 1;
#define TT_EV gTtEv
#define STAGED gStaged
#define THREAT gThreat
#define TT_MASK gTtMask
#define QTT gQtt
#endif
#ifndef LMR
#define LMR 1
#endif
#ifndef LMR_DIV
#define LMR_DIV 1.1
#endif
#ifndef QMAX
#define QMAX 6
#endif
#ifndef ASP          // aspiration window half-width (0 = off)
#define ASP 60
#endif
#ifndef NMP          // null move (a real WAIT) reduction base (0 = off)
#define NMP 2
#endif
#ifndef RFP          // reverse futility margin per depth (0 = off)
#define RFP 150
#endif
#ifndef RFP_MAXD
#define RFP_MAXD 4
#endif
#ifndef FUT          // futility margin per depth, depth <= 2 (0 = off)
#define FUT 450
#endif
#ifndef LMP          // late move pruning base count (0 = off)
#define LMP 2
#endif
#ifndef LMP_D
#define LMP_D 3
#endif
#ifndef FUT_D        // futility up to this depth
#define FUT_D 2
#endif
#ifndef NOWAIT       // 1: WAIT is only generated when there is no other move
#define NOWAIT 0
#endif
#ifndef NEUSHOT      // 0: all shots at neutrals, 1: only killing ones, 2: none
#define NEUSHOT 0
#endif
#ifndef QDMG         // quiescence also searches shots on enemy units doing at least QDMG damage (0 = killing shots only)
#define QDMG 1
#endif
#ifndef LMP_LIN      // late move pruning threshold LMP + LMP_LIN * depth instead of LMP + depth^2 (0 = off)
#define LMP_LIN 0
#endif
#ifndef HPRUNE       // history pruning: skip quiet moves with history below -HPRUNE*depth at depth <= 4 (0 = off)
#define HPRUNE 0
#endif
#ifndef THREAT       // leader steps that threaten a conversion are never pruned and reduced less (0 = off)
#define THREAT 0
#endif
#ifndef IIR          // internal iterative reduction from this depth (0 = off)
#define IIR 0
#endif
#ifndef UNITV
#define UNITV 1000
#endif
#ifndef HPV
#define HPV 40
#endif
#ifndef LEADV
#define LEADV 150
#endif
#ifndef LHPV
#define LHPV 30
#endif
#ifndef NEUV
#define NEUV 300
#endif
#ifndef NEUD
#define NEUD 25
#endif

#ifndef NN_H         // hidden size of the neural evaluation (0 = handcrafted evaluation)
#define NN_H 0
#endif
#ifndef NN_SCALE     // search units per logit; equals the training score scale (train.py --k)
#define NN_SCALE 800
#endif
//@NET_DATA@

static const int MATE = 30000, INF = 32000;

// ---- static tables ---------------------------------------------------------------------
static bool wallAt[NC];
static int8_t nbr[NC][4];            // referee order: up, right, down, left; -1 = off board / obstacle
static uint8_t mdist[NC][NC];        // manhattan
static uint8_t bfsD[NC][NC];         // walking distance around obstacles (units ignored)
static int8_t pathLen[NC][NC];       // bullet line from shooter to target, cells strictly between,
static int8_t pathCell[NC][NC][6];   //   ordered from the shooter, cut at the first obstacle
static bool pathWall[NC][NC];        // an obstacle is on the line: the target itself is never hit
static U128 pathMask[NC][NC];
static U128 rangeMask[NC];           // cells within manhattan distance 1..6
static U64 zPos[NU][NC], zHp[NU][11], zOwn[NU][3], zTurn[MAXTURN + 2];

static inline U128 bit(int c) { return (U128)1 << c; }

#if NN_H
// ---- neural evaluation: weights and feature indexing (see tools/train.py) -----------------
enum { NN_B = 4, NUF = 5 * 4 * NC, NF = NUF + NC, QA = 255, QB = 64 };
static int16_t nnW[NF][NN_H], nnBias[NN_H], nnO[NN_B][2][NN_H];
static int32_t nnOB[NN_B];
static int16_t nnBase[2][NN_H];   // bias + obstacle features of this map, per perspective
static const int8_t HPB[11] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3};
static inline int feat(int p, int u, int own, int hp, int pos) {
    int typ = u < 2 ? (u != p) : own == p ? 2 : own == 2 ? 4 : 3;
    int sq = p ? pos / W * W + (W - 1 - pos % W) : pos;
    return (typ * 4 + HPB[hp]) * NC + sq;
}
static bool nnLoaded;
static void nnRead(const int16_t* v) {   // v: the int16 stream of the .bin file, biases split in two
    memcpy(nnW, v, sizeof nnW); v += NF * NN_H;
    memcpy(nnBias, v, sizeof nnBias); v += NN_H;
    memcpy(nnO, v, sizeof nnO); v += NN_B * 2 * NN_H;
    for (int b = 0; b < NN_B; b++) nnOB[b] = (int32_t)v[2 * b] * 16384 + v[2 * b + 1];
    nnLoaded = true;
}
#endif

// Bullet line exactly as Board.checkBulletPath: Bresenham drawn from the lower-y end
// (from the target when y is equal). Returns the cells strictly between, shooter side first.
static int lineCells(int from, int to, int8_t* out) {
    int fx = from % W, fy = from / W, tx = to % W, ty = to / W;
    bool fwd = fy < ty;
    int x0 = fwd ? fx : tx, y0 = fwd ? fy : ty, x1 = fwd ? tx : fx, y1 = fwd ? ty : fy;
    int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, cx = x0, cy = y0, n = 0;
    int8_t tmp[32];
    for (;;) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx) { err += dx; cy += sy; }
        if (cx == x1 && cy == y1) break;
        tmp[n++] = (int8_t)(cy * W + cx);
    }
    for (int i = 0; i < n; i++) out[i] = fwd ? tmp[i] : tmp[n - 1 - i];
    return n;
}

static void initTables(const char grid[H][W + 2]) {
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int c = 0; c < NC; c++) wallAt[c] = grid[c / W][c % W] == 'x';
    for (int c = 0; c < NC; c++)
        for (int d = 0; d < 4; d++) {
            int x = c % W + DX[d], y = c / W + DY[d];
            nbr[c][d] = (x < 0 || x >= W || y < 0 || y >= H || wallAt[y * W + x]) ? -1 : (int8_t)(y * W + x);
        }
    for (int a = 0; a < NC; a++) {
        rangeMask[a] = 0;
        for (int b = 0; b < NC; b++) {
            mdist[a][b] = (uint8_t)(abs(a % W - b % W) + abs(a / W - b / W));
            if (a != b && mdist[a][b] <= RANGE) rangeMask[a] |= bit(b);
            bfsD[a][b] = 99;
            pathLen[a][b] = 0; pathWall[a][b] = false; pathMask[a][b] = 0;
        }
    }
    for (int a = 0; a < NC; a++) {
        if (wallAt[a]) continue;
        int q[NC], qh = 0, qt = 0;
        q[qt++] = a; bfsD[a][a] = 0;
        while (qh < qt) {
            int c = q[qh++];
            for (int d = 0; d < 4; d++) {
                int n = nbr[c][d];
                if (n >= 0 && bfsD[a][n] == 99) { bfsD[a][n] = bfsD[a][c] + 1; q[qt++] = n; }
            }
        }
        for (int b = 0; b < NC; b++) {
            if (a == b || mdist[a][b] > RANGE) continue;
            int8_t cells[32];
            int n = lineCells(a, b, cells), k = 0;
            for (int i = 0; i < n; i++) {
                if (wallAt[cells[i]]) { pathWall[a][b] = true; break; }
                pathCell[a][b][k++] = cells[i];
                pathMask[a][b] |= bit(cells[i]);
            }
            pathLen[a][b] = (int8_t)k;
        }
    }
#if NN_H
    for (int p = 0; p < 2; p++) {
        memcpy(nnBase[p], nnBias, sizeof nnBias);
        for (int c = 0; c < NC; c++)
            if (wallAt[c]) {
                const int16_t* w = nnW[NUF + (p ? c / W * W + (W - 1 - c % W) : c)];
                for (int i = 0; i < NN_H; i++) nnBase[p][i] += w[i];
            }
    }
#endif
    U64 x = 0x9E3779B97F4A7C15ULL;
    auto r64 = [&]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
    for (int u = 0; u < NU; u++) {
        for (int c = 0; c < NC; c++) zPos[u][c] = r64();
        for (int h = 0; h < 11; h++) zHp[u][h] = r64();
        for (int o = 0; o < 3; o++) zOwn[u][o] = r64();
    }
    for (int t = 0; t < MAXTURN + 2; t++) zTurn[t] = r64();
}

// ---- state -------------------------------------------------------------------------------
// Unit ids as in the referee: 0 and 1 are the cult leaders of player 0 and 1, 2..13 cultists.
// A dead unit keeps its owner (a dead neutral still takes a slot in the referee's neutral list).
struct State {
    U128 occ;
    U64 hash;          // units only; see key()
    uint32_t seed;     // referee LCG state (model.Random), starts at 42
    int16_t turn;      // plies played; player turn % 2 is to move
    int8_t cnt[2];
    int8_t pos[NU], hp[NU], own[NU];
    int8_t at[NC];     // unit id on the cell, -1 if none
#if NN_H
    int16_t acc[2][NN_H];   // first-layer sums, perspective of player 0 and 1
#endif
};
#if NN_H
static inline void accAdd(State& s, int u, int own, int hp, int pos, int sign) {
    for (int p = 0; p < 2; p++) {
        const int16_t* w = nnW[feat(p, u, own, hp, pos)];
        int16_t* a = s.acc[p];
        if (sign > 0) for (int i = 0; i < NN_H; i++) a[i] += w[i];
        else for (int i = 0; i < NN_H; i++) a[i] -= w[i];
    }
}
// A unit changes cell (or hp / owner): one pass, acc += new row - old row.
static inline void accSwap(State& s, int u, int own0, int hp0, int pos0, int own1, int hp1, int pos1) {
    for (int p = 0; p < 2; p++) {
        const int16_t *w0 = nnW[feat(p, u, own0, hp0, pos0)], *w1 = nnW[feat(p, u, own1, hp1, pos1)];
        if (w0 == w1) continue;
        int16_t* a = s.acc[p];
        for (int i = 0; i < NN_H; i++) a[i] += w1[i] - w0[i];
    }
}
#else
static inline void accAdd(State&, int, int, int, int, int) {}
static inline void accSwap(State&, int, int, int, int, int, int, int) {}
#endif

static inline U64 key(const State& s) {
    return s.hash ^ ((U64)s.seed * 0xD6E8FEB86659FD93ULL) ^ zTurn[s.turn];
}
static inline int rnd(State& s, int bound) {
    s.seed = (s.seed * 1103515245u + 12345u) & 0x7fffffffu;
    return (int)(s.seed % (uint32_t)bound);
}
static inline bool over(const State& s) { return s.turn >= MAXTURN || s.cnt[0] < 1 || s.cnt[1] < 1; }

static void rehash(State& s) {
    s.hash = 0; s.occ = 0;
    memset(s.at, -1, sizeof s.at);
    for (int u = 0; u < NU; u++) {
        s.hash ^= zHp[u][s.hp[u]] ^ zOwn[u][s.own[u]];
        if (s.hp[u] > 0) { s.hash ^= zPos[u][s.pos[u]]; s.occ |= bit(s.pos[u]); s.at[s.pos[u]] = (int8_t)u; }
    }
#if NN_H
    memcpy(s.acc, nnBase, sizeof nnBase);
    for (int u = 0; u < NU; u++) if (s.hp[u] > 0) accAdd(s, u, s.own[u], s.hp[u], s.pos[u], 1);
#endif
}
static inline void moveUnit(State& s, int u, int to) {
    int from = s.pos[u];
    accSwap(s, u, s.own[u], s.hp[u], from, s.own[u], s.hp[u], to);
    s.hash ^= zPos[u][from] ^ zPos[u][to];
    s.occ ^= bit(from) | bit(to);
    s.at[from] = -1; s.at[to] = (int8_t)u; s.pos[u] = (int8_t)to;
}
// Unit hit by a bullet fired from cell `from` at the unit on cell `to`; -1 = obstacle.
static inline int resolveHit(const State& s, int from, int to) {
    if (!(pathMask[from][to] & s.occ)) return pathWall[from][to] ? -1 : s.at[to];
    for (int i = 0;; i++) {
        int u = s.at[pathCell[from][to][i]];
        if (u >= 0) return u;
    }
}

static inline Move mk(int type, int unit, int arg) { return (Move)(type | unit << 2 | arg << 6); }
static inline int mType(Move m) { return m & 3; }
static inline int mUnit(Move m) { return (m >> 2) & 15; }
static inline int mArg(Move m) { return m >> 6; }   // MOVE: cell, SHOOT/CONVERT: target id

// One referee turn: the player's action, then the neutral move. Actions are already
// resolved (adjacent moves, targets in range): the bot never relies on referee pathfinding.
static void apply(State& s, Move m) {
    int deadCell = -1, u = mUnit(m), a = mArg(m);
    switch (mType(m)) {
    case MOVE: moveUnit(s, u, a); break;
    case SHOOT: {
        int h = resolveHit(s, s.pos[u], s.pos[a]);
        if (h >= 0) {
            int nh = max(0, s.hp[h] - (7 - mdist[s.pos[u]][s.pos[h]]));
            s.hash ^= zHp[h][s.hp[h]] ^ zHp[h][nh];
            if (nh) accSwap(s, h, s.own[h], s.hp[h], s.pos[h], s.own[h], nh, s.pos[h]);   // same hp bucket: nothing to do
            else accAdd(s, h, s.own[h], s.hp[h], s.pos[h], -1);
            s.hp[h] = (int8_t)nh;
            if (!nh) {
                deadCell = s.pos[h];   // the body blocks its tile until the end of the turn
                s.hash ^= zPos[h][deadCell];
                s.occ ^= bit(deadCell); s.at[deadCell] = -1;
                if (s.own[h] != 2) s.cnt[s.own[h]]--;
            }
        }
        break;
    }
    case CONVERT: {
        int me = s.own[u];
        if (s.own[a] != 2) s.cnt[s.own[a]]--;
        s.hash ^= zOwn[a][s.own[a]] ^ zOwn[a][me];
        accSwap(s, a, s.own[a], s.hp[a], s.pos[a], me, s.hp[a], s.pos[a]);
        s.own[a] = (int8_t)me; s.cnt[me]++;
        break;
    }
    }
    // Referee.moveNeutralUnit: index = rand(12) into the list of neutral-owned units (dead ones
    // included); out of range or dead: nobody moves and the direction is not drawn.
    int8_t list[12]; int n = 0;
    for (int i = 2; i < NU; i++) if (s.own[i] == 2) list[n++] = (int8_t)i;
    if (n) {
        int idx = rnd(s, 12);
        if (idx < n && s.hp[list[idx]] > 0) {
            int v = list[idx], p = s.pos[v], k = 0; int8_t opt[4];
            for (int d = 0; d < 4; d++) {
                int c = nbr[p][d];
                if (c >= 0 && s.at[c] < 0 && c != deadCell) opt[k++] = (int8_t)c;
            }
            if (!k) rnd(s, 1); else moveUnit(s, v, opt[rnd(s, k)]);
        }
    }
    s.turn++;
}

// ---- move generation ---------------------------------------------------------------------
// mode 0: search (drops shots that hit nothing or hit an own unit, except a killing shot on
//         an own cultist standing next to the enemy leader, which denies a conversion)
// mode 1: every distinct legal effect (to recognise the opponent's action)
// mode 2: tactical only (conversions, killing shots on enemy units)
static int gen(const State& s, Move* mv, int* sc, int mode) {
    int me = s.turn & 1, opp = me ^ 1, n = 0;
    for (int u = 0; u < NU; u++) {
        if (s.hp[u] <= 0 || s.own[u] != me) continue;
        int p = s.pos[u];
        if (mode != 2) {
            // THREAT 1: leader steps are scored by walking distance to the nearest convertible unit
            // (next to one = a threat, never pruned). THREAT 2: only "next to one", from the neighbours.
            int best0 = 99;
            if (THREAT == 1 && u < 2 && mode == 0)
                for (int t = 2; t < NU; t++)
                    if (s.hp[t] > 0 && s.own[t] != me) best0 = min(best0, (int)bfsD[p][s.pos[t]]);
            for (int d = 0; d < 4; d++) {
                int c = nbr[p][d];
                if (c < 0 || s.at[c] >= 0) continue;
                int v = 0;
                if (best0 < 99) {
                    int b1 = 99;
                    for (int t = 2; t < NU; t++)
                        if (s.hp[t] > 0 && s.own[t] != me) b1 = min(b1, (int)bfsD[c][s.pos[t]]);
                    if (b1 == 1) v = 430000;            // next to a convertible unit: a threat
                    else if (b1 < best0) v = 50000;     // approaching one
                } else if (THREAT == 2 && u < 2 && mode == 0) {
                    for (int e = 0; e < 4; e++) {
                        int c2 = nbr[c][e], t = c2 >= 0 ? s.at[c2] : -1;
                        if (t >= 2 && s.own[t] != me) { v = 430000; break; }
                    }
                }
                mv[n] = mk(MOVE, u, c); sc[n++] = v;
            }
        }
        if (u < 2) {
            for (int d = 0; d < 4; d++) {
                int c = nbr[p][d], t = c >= 0 ? s.at[c] : -1;
                if (t >= 2 && s.own[t] != me) { mv[n] = mk(CONVERT, u, t); sc[n++] = (s.own[t] == opp ? 900000 : 800000) + s.hp[t]; }
            }
            continue;
        }
        U128 cand = s.occ & rangeMask[p];
        unsigned seen = 0;
        for (int half = 0; half < 2; half++) {
            U64 b = half ? (U64)(cand >> 64) : (U64)cand;
            while (b) {
                int c = __builtin_ctzll(b) + half * 64; b &= b - 1;
                int h = resolveHit(s, p, c);
                if (h < 0 || (seen >> h & 1)) continue;
                seen |= 1u << h;
                int dmg = 7 - mdist[p][s.pos[h]];
                bool kill = dmg >= s.hp[h];
                int o = s.own[h], v;
                if (o == me) {
                    if (mode == 2) continue;
                    if (mode == 0 && !(kill && h >= 2 && s.hp[opp] > 0 && mdist[s.pos[h]][s.pos[opp]] == 1)) continue;
                    v = -500000;
                } else if (o == 2) {
                    if (mode == 2) continue;
                    if (mode == 0 && NEUSHOT && (NEUSHOT == 2 || !kill)) continue;
                    v = kill ? -1000 : -2000 + dmg;
                } else {
                    if (mode == 2 && !kill && !(QDMG && dmg >= QDMG)) continue;
                    v = (kill ? 850000 : 500000) + dmg * 1000 + (h < 2 ? 500 : 0) - s.hp[h];
                }
                mv[n] = mk(SHOOT, u, s.at[c]); sc[n++] = v;
            }
        }
    }
    if (mode != 2 && !(NOWAIT && mode == 0 && n)) { mv[n] = mk(WAIT, 0, 0); sc[n++] = -900000; }
    return n;
}

static string moveStr(const State& s, Move m) {
    char buf[40];
    int u = mUnit(m), a = mArg(m);
    switch (mType(m)) {
    case MOVE: snprintf(buf, sizeof buf, "%d MOVE %d %d", u, a % W, a / W); break;
    case SHOOT: snprintf(buf, sizeof buf, "%d SHOOT %d", u, a); break;
    case CONVERT: snprintf(buf, sizeof buf, "%d CONVERT %d", u, a); break;
    default: snprintf(buf, sizeof buf, "WAIT");
    }
    (void)s;
    return buf;
}

// ---- evaluation --------------------------------------------------------------------------
// Score for player 0. Material (unit count decides the game), hit points, and how close each
// living leader is to the neutrals it could still convert.
static int evalP0(const State& s) {
    int sc = UNITV * (s.cnt[0] - s.cnt[1]);
    int8_t neu[12]; int nn = 0;
    for (int u = 2; u < NU; u++) {
        if (s.hp[u] <= 0) continue;
        if (s.own[u] == 2) neu[nn++] = s.pos[u];
        else sc += (s.own[u] == 0 ? HPV : -HPV) * s.hp[u];
    }
    for (int p = 0; p < 2; p++) {
        if (s.hp[p] <= 0) continue;
        int v = LEADV + LHPV * s.hp[p];
        for (int i = 0; i < nn; i++) v += max(0, NEUV - NEUD * (int)bfsD[s.pos[p]][neu[i]]);
        sc += p == 0 ? v : -v;
    }
    return sc;
}
#if NN_H
static inline int nnEval(const State& s) {
    int stm = s.turn & 1, left = MAXTURN - s.turn, b = (left <= 70) + (left <= 30) + (left <= 10);
    const int16_t *a0 = s.acc[stm], *a1 = s.acc[stm ^ 1], *o0 = nnO[b][0], *o1 = nnO[b][1];
    int sum = 0;
#if defined(__AVX2__) && NN_H % 16 == 0 && !defined(NO_SIMD)
    // v = clamp(acc, 0, 255); sum += (v * o) * v, 16 lanes at a time: v * o fits int16 (255 * 127).
#ifdef CGBENCH
    if (!gSimd) {
        for (int i = 0; i < NN_H; i++) {
            int v = min(max((int)a0[i], 0), (int)QA), w = min(max((int)a1[i], 0), (int)QA);
            sum += v * o0[i] * v + w * o1[i] * w;
        }
    } else {
#endif
    const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
    __m256i acc = zero;
    for (int i = 0; i < NN_H; i += 16) {
        __m256i v = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i*)(a0 + i)), zero), qa);
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(_mm256_mullo_epi16(v, _mm256_loadu_si256((const __m256i*)(o0 + i))), v));
        __m256i w = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i*)(a1 + i)), zero), qa);
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(_mm256_mullo_epi16(w, _mm256_loadu_si256((const __m256i*)(o1 + i))), w));
    }
    __m128i h = _mm_add_epi32(_mm256_castsi256_si128(acc), _mm256_extracti128_si256(acc, 1));
    h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
    h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
    sum = _mm_cvtsi128_si32(h);
#ifdef CGBENCH
    }
#endif
#else
    for (int i = 0; i < NN_H; i++) {
        int v = min(max((int)a0[i], 0), (int)QA), w = min(max((int)a1[i], 0), (int)QA);
        sum += v * o0[i] * v + w * o1[i] * w;
    }
#endif
    int z = (int)((long long)(sum / QA + nnOB[b]) * NN_SCALE / (QA * QB));
    return z > 20000 ? 20000 : z < -20000 ? -20000 : z;
}
static inline int evalStm(const State& s) {
    if (!nnLoaded) return (s.turn & 1) ? -evalP0(s) : evalP0(s);
    return nnEval(s);
}
#else
static inline int evalStm(const State& s) { return (s.turn & 1) ? -evalP0(s) : evalP0(s); }
#endif
static inline int terminal(const State& s, int ply) {
    int d = s.cnt[0] - s.cnt[1];
    if (s.turn & 1) d = -d;
    return d > 0 ? MATE - ply : d < 0 ? -MATE + ply : 0;
}

// ---- search ------------------------------------------------------------------------------
struct TTE { uint32_t key; Move move; int16_t score; int16_t ev; int8_t depth; uint8_t flag; };   // 12 bytes; ev = static eval
#ifndef TT_EV        // keep the static evaluation in the table: later iterations revisit the node without evaluating it again
#define TT_EV 1
#endif
static const int16_t EV_NONE = -32768;
enum { TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };
static TTE* tt;
#ifndef CGBENCH
static const U64 TT_MASK = (1ULL << TT_BITS) - 1;
#endif
static int hist[2][1 << 13];
static Move killer[MAXTURN + 8][2];
static long long nodes, nodeLimit;
static bool stopped;
static chrono::steady_clock::time_point deadline;
static int lmrTab[64][64];

static inline bool timeUp() {
    if (nodeLimit) return nodes >= nodeLimit;
    return (nodes & 255) == 0 && chrono::steady_clock::now() >= deadline;
}
static inline int pick(Move* mv, int* sc, int i, int n) {
    int b = i;
    for (int j = i + 1; j < n; j++) if (sc[j] > sc[b]) b = j;
    swap(mv[i], mv[b]); swap(sc[i], sc[b]);
    return i;
}

#ifndef IMP          // "improving": static eval above the one two plies ago relaxes pruning, below tightens it (0 = off)
#define IMP 0
#endif
#ifndef HMAL         // history penalty for quiet moves tried before the one that cut (0 = off)
#define HMAL 1
#endif
#ifndef HLMR         // late move reduction changes by history / HLMR (0 = off)
#define HLMR 0
#endif
#ifndef SE           // singular extension margin per depth (0 = off)
#define SE 6
#endif
#ifndef SE_DEPTH
#define SE_DEPTH 6
#endif
#ifndef TLMR         // late non-killing shots on enemy units are reduced by half the quiet-move reduction (0 = off)
#define TLMR 0
#endif
#ifndef PVLMR        // one ply less reduction at PV nodes (0 = off)
#define PVLMR 0
#endif
#ifndef HASHQ        // a quiet hash move that cuts also updates killers and history (0 = off)
#define HASHQ 0
#endif
#ifndef QDELTA       // quiescence skips a non-killing shot when stand + QDELTA + damage * QDHP cannot reach alpha (0 = off)
#define QDELTA 0
#endif
#ifndef QDHP
#define QDHP 80
#endif
#ifndef CMH          // countermove: the quiet move that last refuted the previous move is tried after the killers (0 = off)
#define CMH 0
#endif
#ifndef QTT          // transposition table probe/store in quiescence (0 = off; +13 Elo in self-play, unproven in the arena)
#define QTT 0
#endif
#ifndef QTTS         // quiescence also stores stand-pat cutoffs (0 = off)
#define QTTS 0
#endif
#ifndef TT2          // two-entry buckets: probe both, replace the shallower / older entry (0 = one entry, always replace)
#define TT2 0
#endif
static int16_t evStack[MAXTURN + 16];
static Move mvStack[MAXTURN + 16];
static Move counterMv[2][1 << 13];
static uint8_t ttAge;
static inline TTE* ttProbe(U64 k) {
#if TT2
    TTE* b = &tt[(k & TT_MASK) & ~1ULL];
    uint32_t kk = (uint32_t)(k >> 32);
    if (b[0].key == kk) return b;
    if (b[1].key == kk) return b + 1;
    int v0 = b[0].depth - 4 * ((ttAge - (b[0].flag >> 2)) & 63), v1 = b[1].depth - 4 * ((ttAge - (b[1].flag >> 2)) & 63);
    return v0 <= v1 ? b : b + 1;
#else
    return &tt[k & TT_MASK];
#endif
}
static int qsearch(const State& s, int alpha, int beta, int ply, int qd) {
    if (over(s)) return terminal(s, ply);
    nodes++;
    // QTT: transposition table in quiescence: bounds from any earlier visit, and the stored static eval.
    U64 k = 0;
    TTE* e = nullptr;
    bool hit = false;
    int stand = EV_NONE, a0 = alpha;
    if (QTT) {
        k = key(s);
        e = ttProbe(k);
        hit = e->key == (uint32_t)(k >> 32);
        if (hit && (e->flag & 3)) {
            int v = e->score, f = e->flag & 3;
            if (v > MATE - 400) v -= ply; else if (v < -MATE + 400) v += ply;
            if (f == TT_EXACT || (f == TT_LOWER && v >= beta) || (f == TT_UPPER && v <= alpha)) return v;
            if (TT_EV) stand = e->ev;
        }
    }
    if (stand == EV_NONE) stand = evalStm(s);
    int ev = stand;
    if (stand >= beta || qd >= QMAX) {
        if (QTTS && QTT && stand >= beta && (!hit ? e->depth <= 0 || TT2 : e->depth <= 0) && abs(stand) < MATE - 400) {   // stand-pat cut: a lower bound
            e->key = (uint32_t)(k >> 32); e->move = NOMOVE; e->score = (int16_t)stand; e->ev = (int16_t)ev; e->depth = 0;
            e->flag = (uint8_t)(TT_LOWER | (TT2 ? (ttAge & 63) << 2 : 0));
        }
        return stand;
    }
    if (stand > alpha) alpha = stand;
    Move mv[64]; int sc[64];
    int n = gen(s, mv, sc, 2);
    for (int i = 0; i < n; i++) {
        pick(mv, sc, i, n);
        if (QDELTA && sc[i] >= 500000 && sc[i] < 800000 && stand + QDELTA + (sc[i] - 500000) / 1000 * QDHP < alpha) continue;
        State c = s;
        apply(c, mv[i]);
        int v = -qsearch(c, -beta, -alpha, ply + 1, qd + 1);
        if (v > stand) { stand = v; if (v > alpha) { alpha = v; if (v >= beta) break; } }
    }
    if (QTT && (!hit ? e->depth <= 0 || TT2 : e->depth <= 0)) {
        int st = stand;
        if (st > MATE - 400) st += ply; else if (st < -MATE + 400) st -= ply;
        e->key = (uint32_t)(k >> 32); e->move = NOMOVE; e->score = (int16_t)st; e->ev = (int16_t)ev; e->depth = 0;
        e->flag = (uint8_t)((stand >= beta ? TT_LOWER : stand > a0 ? TT_EXACT : TT_UPPER) | (TT2 ? (ttAge & 63) << 2 : 0));
    }
    return stand;
}

#ifndef PRUNE_BREAK  // leave the move loop at the first pruned quiet move instead of picking through the rest
#define PRUNE_BREAK 1
#endif
#ifndef STAGED       // try the hash move before generating moves
#define STAGED 1
#endif
// A move taken from the transposition table may belong to another position (key collision).
static bool legalMove(const State& s, Move m) {
    int me = s.turn & 1, u = mUnit(m), a = mArg(m);
    switch (mType(m)) {
    case MOVE:
        return u < NU && a < NC && s.hp[u] > 0 && s.own[u] == me && !wallAt[a] && s.at[a] < 0 && mdist[s.pos[u]][a] == 1;
    case SHOOT:
        return u >= 2 && u < NU && a < NU && a != u && s.hp[u] > 0 && s.own[u] == me && s.hp[a] > 0 && mdist[s.pos[u]][s.pos[a]] <= RANGE;
    case CONVERT:
        return u == me && a >= 2 && a < NU && s.hp[u] > 0 && s.hp[a] > 0 && s.own[a] != me && mdist[s.pos[u]][s.pos[a]] == 1;
    default:
        return m == mk(WAIT, 0, 0);
    }
}
static int search(const State& s, int depth, int alpha, int beta, int ply, bool allowNull = true, Move excl = NOMOVE) {
    if (over(s)) return terminal(s, ply);
    if (depth <= 0) return qsearch(s, alpha, beta, ply, 0);
    nodes++;
    if (timeUp()) { stopped = true; return 0; }
    bool pv = beta - alpha > 1;
    U64 k = key(s);
    TTE* e = ttProbe(k);
    Move ttm = NOMOVE;
    int ev = EV_NONE, ttScore = 0, ttDepth = -1, ttFlag = 0;
    if (e->key == (uint32_t)(k >> 32)) {
        ttm = e->move;
        if (TT_EV && (e->flag & 3)) ev = e->ev;
        ttScore = e->score; ttDepth = e->depth; ttFlag = e->flag & 3;
        if (ttScore > MATE - 400) ttScore -= ply; else if (ttScore < -MATE + 400) ttScore += ply;
        if (e->depth >= depth && !pv && excl == NOMOVE) {
            int v = e->score;
            if (v > MATE - 400) v -= ply; else if (v < -MATE + 400) v += ply;
            if (ttFlag == TT_EXACT || (ttFlag == TT_LOWER && v >= beta) || (ttFlag == TT_UPPER && v <= alpha)) return v;
        }
    }
    if (ev == EV_NONE) ev = evalStm(s);
    evStack[ply] = (int16_t)ev;
    bool improving = IMP && ply >= 2 && ev > evStack[ply - 2];
    if (!pv && abs(beta) < MATE - 400 && excl == NOMOVE) {
        if (RFP && depth <= RFP_MAXD && ev - RFP * (depth - improving) >= beta) return ev;
        if (NMP && allowNull && depth >= 3 && ev >= beta) {
            State c = s;
            apply(c, mk(WAIT, 0, 0));
            mvStack[ply] = mk(WAIT, 0, 0);
            int v = -search(c, depth - 1 - NMP - depth / 4, -beta, -beta + 1, ply + 1, false);
            if (stopped) return 0;
            if (v >= beta && v < MATE - 400) return v;
        }
    }
    if (IIR && depth >= IIR && ttm == NOMOVE) depth--;
    // The hash move is searched before any move is generated: most cutoffs come from it.
    bool ttOk = STAGED && ttm != NOMOVE && legalMove(s, ttm);
    // Singular extension: if no other move comes close to the hash move at reduced depth, search
    // the hash move one ply deeper; if another move also beats beta, cut (multi-cut).
    int ext = 0;
    if (SE && depth >= SE_DEPTH && ttOk && excl == NOMOVE && ply > 0 && ttDepth >= depth - 3 && (ttFlag == TT_LOWER || ttFlag == TT_EXACT) &&
        abs(ttScore) < MATE - 400) {
        int sb = ttScore - SE * depth;
        int v = search(s, (depth - 1) / 2, sb - 1, sb, ply, false, ttm);
        if (stopped) return 0;
        if (v < sb) ext = 1;
        else if (sb >= beta) return sb;
    }
    Move tried[64];
    int nt = 0;
    Move mv[256]; int sc[256];
    int n = 0, gi = 0, me = s.turn & 1;
    Move cm = CMH && ply > 0 ? counterMv[me][mvStack[ply - 1] & 8191] : NOMOVE;
    bool generated = false;
    int best = -INF, a0 = alpha;
    Move bm = NOMOVE;
    for (int i = 0;; i++) {
        Move m;
        int msc;
        if (i == 0 && ttOk) { m = ttm; msc = 2000000; }
        else {
            if (!generated) {
                generated = true;
                n = gen(s, mv, sc, 0);
                for (int j = 0; j < n; j++) {
                    Move g = mv[j];
                    if (g == ttm) {
                        sc[j] = ttOk ? -2000000000 : 2000000;   // already searched: sorts last and ends the loop
                    } else if (sc[j] > -100000 && sc[j] < 100000) {   // quiet steps and shots at neutrals
                        if (g == killer[ply][0]) sc[j] = 400000;
                        else if (g == killer[ply][1]) sc[j] = 399000;
                        else if (CMH && g == cm) sc[j] = 398000;
                        else sc[j] += hist[me][g & 8191];
                    }
                }
            }
            if (gi >= n) break;
            pick(mv, sc, gi, n);
            m = mv[gi]; msc = sc[gi]; gi++;
            if (msc == -2000000000) break;
        }
        if (m == excl) continue;
        bool quiet = msc < 450000, threat = quiet && msc >= 420000;
        if (HASHQ && msc == 2000000 && mType(m) == MOVE) quiet = true;
        if (i > 0 && quiet && !threat && !pv && best > -MATE + 400) {
            // Moves come in descending score, so everything after a pruned quiet move is a quiet move
            // too, and both conditions stay true (i grows, alpha only rises): stop instead of skipping.
            if (LMP && depth <= LMP_D && i >= (LMP + (LMP_LIN ? LMP_LIN * depth : depth * depth)) / (IMP && !improving ? 2 : 1)) { if (PRUNE_BREAK) break; continue; }
            if (FUT && depth <= FUT_D && ev + FUT * depth <= alpha) { if (PRUNE_BREAK) break; continue; }
            if (HPRUNE && depth <= 4 && msc < -HPRUNE * depth) continue;
        }
        State c = s;
        apply(c, m);
        mvStack[ply] = m;
        int v;
        if (quiet && nt < 64) tried[nt++] = m;
        if (i == 0) v = -search(c, depth - 1 + ext, -beta, -alpha, ply + 1);
        else {
            int r = 0;
            if (LMR && quiet && depth >= 3 && i >= 3) {
                r = lmrTab[min(depth, 63)][min(i, 63)];
                if (threat) r = r / 2;
                if (IMP && !improving) r++;
#if HLMR
                r -= hist[me][m & 8191] / HLMR;
#endif
                if (PVLMR && pv) r--;
                if (r < 0) r = 0;
                if (r > depth - 2) r = depth - 2;
            } else if (TLMR && msc >= 500000 && msc < 800000 && depth >= 3 && i >= 4) {
                r = lmrTab[min(depth, 63)][min(i, 63)] / 2;
                if (r > depth - 2) r = depth - 2;
            }
            v = -search(c, depth - 1 - r, -alpha - 1, -alpha, ply + 1);
            if (v > alpha && r && !stopped) v = -search(c, depth - 1, -alpha - 1, -alpha, ply + 1);
            if (v > alpha && v < beta && !stopped) v = -search(c, depth - 1, -beta, -alpha, ply + 1);
        }
        if (stopped) return 0;
        if (v > best) {
            best = v; bm = m;
            if (v > alpha) {
                alpha = v;
                if (v >= beta) {
                    if (quiet) {
                        if (killer[ply][0] != m) { killer[ply][1] = killer[ply][0]; killer[ply][0] = m; }
                        if (CMH && ply > 0) counterMv[me][mvStack[ply - 1] & 8191] = m;
                        int& h = hist[me][m & 8191];
                        h += depth * depth - h * depth * depth / 16384;
                        if (HMAL)
                            for (int j = 0; j < nt; j++)
                                if (tried[j] != m) {
                                    int& g = hist[me][tried[j] & 8191];
                                    g -= depth * depth + g * depth * depth / 16384;
                                }
                    }
                    break;
                }
            }
        }
    }
    if (excl != NOMOVE) return best == -INF ? alpha : best;      // singular search: nothing is stored
    int st = best;
    if (st > MATE - 400) st += ply; else if (st < -MATE + 400) st -= ply;
    e->key = (uint32_t)(k >> 32); e->move = bm; e->score = (int16_t)st; e->ev = (int16_t)ev; e->depth = (int8_t)depth;
    e->flag = (uint8_t)((best >= beta ? TT_LOWER : best > a0 ? TT_EXACT : TT_UPPER) | (TT2 ? (ttAge & 63) << 2 : 0));
    return best;
}

struct Info { int depth, score; long long nodes; double ms; };
static Info info;

static Move think(const State& root, double budgetMs) {
    auto t0 = chrono::steady_clock::now();
    deadline = t0 + chrono::microseconds((long long)(budgetMs * 1000));
    nodes = 0; stopped = false; ttAge++;
    memset(killer, 0xFF, sizeof killer);
    for (auto& side : hist) for (int& h : side) h /= 2;
    Move mv[256]; int sc[256];
    int n = gen(root, mv, sc, 0);
    Move best = mv[pick(mv, sc, 0, n)];
    info = {0, 0, 0, 0};
    int maxDepth = MAXTURN - root.turn, prev = 0;
    for (int depth = 1; depth <= maxDepth; depth++) {
        int lo = -INF, hi = INF, delta = ASP;
        if (ASP && depth >= 4) { lo = prev - delta; hi = prev + delta; }
        for (;;) {
            int alpha = lo, itScore = -INF;
            Move itBest = NOMOVE;
            for (int i = 0; i < n; i++) if (mv[i] == best) { swap(mv[0], mv[i]); swap(sc[0], sc[i]); }
            for (int i = 0; i < n; i++) {
                if (i) pick(mv, sc, i, n);
                State c = root;
                apply(c, mv[i]);
                mvStack[0] = mv[i];
                int v;
                if (i == 0) v = -search(c, depth - 1, -hi, -alpha, 1);
                else {
                    v = -search(c, depth - 1, -alpha - 1, -alpha, 1);
                    if (v > alpha && !stopped) v = -search(c, depth - 1, -hi, -alpha, 1);
                }
                if (stopped) break;
                if (v > itScore) { itScore = v; if (v > alpha) { alpha = v; itBest = mv[i]; if (v >= hi) break; } }
            }
            // An interrupted iteration still counts once a move has beaten the window's lower bound.
            if (itBest != NOMOVE) { best = itBest; info.score = itScore; }
            if (stopped) break;
            if (itScore <= lo) { lo = max(-INF, lo - delta * 2); delta *= 3; }
            else if (itScore >= hi) { hi = min(INF, hi + delta * 2); delta *= 3; }
            else { prev = itScore; info.depth = depth; break; }
        }
        if (stopped) break;
        if (abs(prev) > MATE - 400) break;
        if (!nodeLimit && chrono::steady_clock::now() - t0 > (deadline - t0) * TM_PCT / 100) break;
    }
    info.nodes = nodes;
    info.ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    return best;
}

static void initNet() {
#if NN_H
#ifdef NN_EMBED
    // Embedded net: a bit stream, 15 bits per character. First-layer weights are int8,
    // everything else int16 (see tools/pack.py).
    static int16_t buf[NF * NN_H + NN_H + NN_B * 2 * NN_H + 2 * NN_B];
    const char16_t* p = NN_DATA;
    U64 acc = 0;
    int nb = 0, k = 0;
    auto byte = [&]() {
        while (nb < 8) { acc |= (U64)(*p++ - NN_OFF) << nb; nb += 15; }
        int v = (int)(acc & 255);
        acc >>= 8; nb -= 8;
        return v;
    };
    for (int i = 0; i < NF * NN_H; i++) buf[k++] = (int8_t)byte();
    for (int i = 0; i < NN_H + NN_B * 2 * NN_H + 2 * NN_B; i++) { int lo = byte(); buf[k++] = (int16_t)(lo | byte() << 8); }
    nnRead(buf);
#endif
#ifdef LOCAL
    if (const char* path = getenv("CW_NET")) {
        FILE* f = fopen(path, "rb");
        size_t n16 = (size_t)NF * NN_H + NN_H + NN_B * 2 * NN_H;
        static int16_t buf[NF * NN_H + NN_H + NN_B * 2 * NN_H + 2 * NN_B];
        int32_t ob[NN_B];
        if (!f || fread(buf, 2, n16, f) != n16 || fread(ob, 4, NN_B, f) != NN_B) { fprintf(stderr, "cannot load net %s\n", path); exit(2); }
        fclose(f);
        for (int b = 0; b < NN_B; b++) { buf[n16 + 2 * b] = (int16_t)(ob[b] >> 14); buf[n16 + 2 * b + 1] = (int16_t)(ob[b] & 16383); }
        nnRead(buf);
    }
#endif
#endif
}
static void initSearch() {
    tt = (TTE*)calloc(1ULL << TT_BITS, sizeof(TTE));
    memset(tt, 0, sizeof(TTE) << TT_BITS);   // touch every page before the first timed turn
    for (int d = 1; d < 64; d++)
        for (int i = 1; i < 64; i++) lmrTab[d][i] = (int)(log(d) * log(i) / LMR_DIV);
}

// ---- protocol: rebuild the exact state (with the referee's RNG) from the inputs --------------
struct Obs { int8_t pos[NU], hp[NU], own[NU]; };   // hp 0 = not listed (dead)

static bool matches(const State& s, const Obs& o) {
    for (int u = 0; u < NU; u++) {
        if ((s.hp[u] > 0) != (o.hp[u] > 0)) return false;
        if (s.hp[u] > 0 && (s.hp[u] != o.hp[u] || s.pos[u] != o.pos[u] || s.own[u] != o.own[u])) return false;
    }
    return true;
}
// Find the opponent action that leads from `prev` to the observed units.
static bool syncFrom(const State& prev, const Obs& o, State& out) {
    Move mv[256]; int sc[256];
    int n = gen(prev, mv, sc, 1);
    for (int i = 0; i < n; i++) {
        State c = prev;
        apply(c, mv[i]);
        if (matches(c, o)) { out = c; return true; }
    }
    return false;
}
static State fromObs(const Obs& o, uint32_t seed, int turn, const State* prev) {
    State s;
    memset(&s, 0, sizeof s);
    s.seed = seed; s.turn = (int16_t)turn;
    for (int u = 0; u < NU; u++) {
        s.hp[u] = o.hp[u]; s.pos[u] = o.pos[u];
        s.own[u] = o.hp[u] > 0 ? o.own[u] : prev ? prev->own[u] : (u < 2 ? u : 2);
        if (o.hp[u] > 0 && s.own[u] < 2) s.cnt[s.own[u]]++;
    }
    rehash(s);
    return s;
}
// Second player: the first input is already one action and one neutral move old. The start
// position is mirror symmetric (unit 2+i mirrors unit 8+i), which gives the candidates.
static bool syncSecond(const Obs& o, State& out) {
    Obs base = o;
    base.pos[0] = 3 * W;
    for (int u = 2; u < NU; u++) base.own[u] = 2;
    auto mir = [](int c) { return c / W * W + (W - 1 - c % W); };
    for (int v = -1; v < 12; v++) {
        Obs cand = base;
        if (v >= 0) {
            int u = 2 + v, twin = u < 8 ? u + 6 : u - 6;
            if (mir(cand.pos[twin]) == cand.pos[u]) continue;
            cand.pos[u] = (int8_t)mir(cand.pos[twin]);
        }
        State s0 = fromObs(cand, 42, 0, nullptr);
        if (syncFrom(s0, o, out)) return true;
    }
    return false;
}

#ifdef LOCAL
#include <random>
#include <vector>
// ---- map generation (Board.initLaymen + initExtraObstacles, with our own RNG) ----------------
struct Map { char grid[H][W + 2]; Obs start; };
static Map makeMap(mt19937& rng) {
    Map mp;
    memset(&mp, 0, sizeof mp);
    bool used[NC] = {false};
    for (int p = 0; p < 2; p++) { mp.start.hp[p] = 10; mp.start.own[p] = (int8_t)p; }
    mp.start.pos[0] = 3 * W; mp.start.pos[1] = 3 * W + W - 1;
    used[3 * W] = used[3 * W + W - 1] = true;
    for (int i = 0; i < 6; i++) {
        int c;
        do c = (int)(rng() % H) * W + 1 + (int)(rng() % 5); while (used[c]);
        int mc = c / W * W + (W - 1 - c % W);
        used[c] = used[mc] = true;
        mp.start.pos[2 + i] = (int8_t)c; mp.start.pos[8 + i] = (int8_t)mc;
        mp.start.hp[2 + i] = mp.start.hp[8 + i] = 10;
        mp.start.own[2 + i] = mp.start.own[8 + i] = 2;
    }
    uniform_real_distribution<double> u01(0, 1);
    for (;;) {
        bool wl[NC] = {false};
        int count = 0;
        for (int col = 0; col < (W - 1) / 2; col++)
            for (int row = 0; row < H; row++)
                if (!used[row * W + col] && u01(rng) < 0.2) { wl[row * W + col] = wl[row * W + W - 1 - col] = true; count += 2; }
        int q[NC], qh = 0, qt = 0, reached = 0;
        bool seen[NC] = {false};
        q[qt++] = 3 * W; seen[3 * W] = true;
        while (qh < qt) {
            int c = q[qh++];
            reached++;
            static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
            for (int d = 0; d < 4; d++) {
                int x = c % W + DX[d], y = c / W + DY[d];
                if (x < 0 || x >= W || y < 0 || y >= H) continue;
                int nb = y * W + x;
                if (!wl[nb] && !seen[nb]) { seen[nb] = true; q[qt++] = nb; }
            }
        }
        if (reached != NC - count) continue;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) mp.grid[y][x] = wl[y * W + x] ? 'x' : '.';
            mp.grid[y][W] = 0;
        }
        return mp;
    }
}

// ---- training data: CW_GEN=games:nodes:seed:file ------------------------------------------------
// Self-play on random maps, 0..8 random opening plies, then both sides search `nodes` nodes.
// One 48-byte record per searched position.
struct Rec {
    uint8_t walls[12];     // bit c = obstacle on cell c
    uint16_t unit[NU];     // pos | hp << 7 | owner << 11 (hp 0 = dead)
    uint8_t turn;          // player turn % 2 is to move
    int8_t result;         // game result for the side to move: 1 / 0 / -1
    int8_t margin;         // final unit difference for the side to move
    uint8_t flags;         // bit 0: the move played is a shot or a conversion
    int16_t score;         // search score for the side to move
    uint8_t pad[2];
};
#ifndef GEN_LATE_T     // data generation: from this turn on, search GEN_LATE_N nodes per move (a deeper teacher for endgames)
#define GEN_LATE_T 999
#endif
#ifndef GEN_LATE_N
#define GEN_LATE_N 0
#endif
// CW_STARTS=file + CW_PART / CW_NPARTS: instead of random maps, play out the start positions of
// the file (M/S pairs written by referee --export with CW_STARTS), every NPARTS-th from PART.
struct Start { char grid[H][W + 2]; Obs ob; uint32_t seed; int turn; };
static vector<Start> loadStarts(const char* path, int part, int nparts) {
    vector<Start> v;
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    static char buf[1 << 16];
    Start cur;
    int idx = 0;
    while (fgets(buf, sizeof buf, f)) {
        if (buf[0] == 'M') {
            for (int y = 0; y < H; y++) { memcpy(cur.grid[y], buf + 2 + y * (W + 1), W); cur.grid[y][W] = 0; }
        } else if (buf[0] == 'S') {
            int pos = 0, id, hp, x, y, ow, n;
            unsigned sd;
            sscanf(buf + 2, "%d %u%n", &cur.turn, &sd, &pos);
            cur.seed = sd;
            memset(&cur.ob, 0, sizeof cur.ob);
            const char* p = buf + 2 + pos;
            while (sscanf(p, " %d,%d,%d,%d,%d%n", &id, &hp, &x, &y, &ow, &n) == 5) {
                cur.ob.hp[id] = (int8_t)hp; cur.ob.pos[id] = (int8_t)(y * W + x); cur.ob.own[id] = (int8_t)ow;
                p += n;
            }
            if (idx++ % nparts == part) v.push_back(cur);
        }
    }
    fclose(f);
    return v;
}

static void runGen(const char* spec) {
    int games; long long nd; unsigned seed; char file[512];
    if (sscanf(spec, "%d:%lld:%u:%511s", &games, &nd, &seed, file) != 4) { fprintf(stderr, "CW_GEN=games:nodes:seed:file\n"); exit(2); }
    FILE* f = fopen(file, "wb");
    if (!f) { perror(file); exit(2); }
    mt19937 rng(seed);
    initNet();
    initSearch();
    vector<Start> starts;
    if (getenv("CW_STARTS")) {
        starts = loadStarts(getenv("CW_STARTS"), atoi(getenv("CW_PART")), atoi(getenv("CW_NPARTS")));
        games = (int)starts.size();
    }
    long long total = 0;
    for (int g = 0; g < games; g++) {
        State s;
        int nOpen;
        if (starts.empty()) {
            Map mp = makeMap(rng);
            initTables(mp.grid);
            s = fromObs(mp.start, 42, 0, nullptr);
            nOpen = (int)(rng() % 9);
        } else {
            const Start& st = starts[g];
            initTables(st.grid);
            s = fromObs(st.ob, st.seed, st.turn, nullptr);
            for (int u = 0; u < NU; u++) s.own[u] = st.ob.own[u];   // dead units keep their owner (neutral list)
            rehash(s);
            nOpen = 0;
        }
        memset(tt, 0, sizeof(TTE) << TT_BITS);
        memset(hist, 0, sizeof hist);
        vector<Rec> recs;
        while (!over(s)) {
            Move m;
            if (s.turn < nOpen) {
                Move mv[256]; int sc[256];
                int n = gen(s, mv, sc, 0);
                m = mv[rng() % (n > 1 ? n - 1 : 1)];   // WAIT is last: never picked when there is anything else
            } else {
                nodeLimit = s.turn >= GEN_LATE_T ? GEN_LATE_N : nd;
                m = think(s, 1e9);
                Rec r;
                memset(&r, 0, sizeof r);
                for (int c = 0; c < NC; c++) if (wallAt[c]) r.walls[c >> 3] |= (uint8_t)(1 << (c & 7));
                for (int u = 0; u < NU; u++) r.unit[u] = (uint16_t)((s.hp[u] > 0 ? s.pos[u] : 0) | s.hp[u] << 7 | s.own[u] << 11);
                r.turn = (uint8_t)s.turn;
                r.flags = mType(m) >= SHOOT;
                r.score = (int16_t)max(-32000, min(32000, info.score));
                recs.push_back(r);
            }
            apply(s, m);
        }
        int d = s.cnt[0] - s.cnt[1];
        for (Rec& r : recs) {
            int sd = (r.turn & 1) ? -d : d;
            r.result = (int8_t)(sd > 0) - (int8_t)(sd < 0);
            r.margin = (int8_t)sd;
        }
        fwrite(recs.data(), sizeof(Rec), recs.size(), f);
        total += (long long)recs.size();
    }
    fclose(f);
    fprintf(stderr, "%d games, %lld positions -> %s\n", games, total, file);
}
#endif

#ifdef CGBENCH
// Node speed on the machine we are running on: three fixed positions, fixed nodes, printed to
// stderr during the first turn. Used in CodinGame test games to compare builds on their CPUs.
static void cgBench() {
    static const char* pos[3] = {
        "x.x.......x.x x...........x x...........x ............. ............. x...........x x...........x|0,7,2,2,0 3,8,3,1,0 4,7,3,4,0 5,10,1,4,0 6,1,1,5,0 10,10,9,2,1 11,10,9,3,1 13,10,9,1,1",
        "............. .x.........x. ...x.x.x.x... ..x.......x.. .....x.x..... ............. x...........x|0,10,0,3,0 1,10,12,3,1 2,10,4,3,2 3,10,4,2,2 4,10,4,0,2 5,10,3,6,2 6,10,4,5,2 7,10,2,0,2 8,10,8,3,2 9,10,8,2,2 10,10,8,0,2 11,10,9,6,2 12,10,8,5,2 13,10,10,0,2",
        "............. x...........x ............. ..x.......x.. ..x.......x.. x....x.x....x ..x.x...x.x..|0,9,3,2,0 1,10,9,1,1 2,10,5,3,2 3,7,2,0,0 4,7,3,3,0 5,10,3,0,0 6,4,1,3,0 8,10,7,3,2 9,10,10,0,1 10,6,9,3,1 11,10,9,0,1 12,10,11,3,1"};
    // tt-eval, staged, threat, simd, table bits, quiescence table
    static const int cfg[8][6] = {{1, 1, 0, 1, 19, 0}, {1, 1, 0, 1, 19, 1}, {1, 1, 0, 1, 19, 0}, {1, 1, 0, 1, 19, 1},
                                  {1, 1, 0, 1, 19, 0}, {1, 1, 0, 1, 19, 1}, {1, 1, 0, 1, 19, 0}, {1, 1, 0, 1, 19, 1}};
    for (int rep = 0; rep < 2; rep++)
    for (int c = 0; c < 8; c++) {
        gTtEv = cfg[c][0]; gStaged = cfg[c][1]; gThreat = cfg[c][2]; gSimd = cfg[c][3]; gTtMask = (1ULL << cfg[c][4]) - 1; gQtt = cfg[c][5];
        long long nn = 0; double ms = 0;
        for (int k = 0; k < 3; k++) {
            char grid[H][W + 2];
            for (int y = 0; y < H; y++) { memcpy(grid[y], pos[k] + y * (W + 1), W); grid[y][W] = 0; }
            initTables(grid);
            Obs o;
            memset(&o, 0, sizeof o);
            const char* p = strchr(pos[k], '|') + 1;
            int id, hp, x, y, ow, n;
            while (sscanf(p, " %d,%d,%d,%d,%d%n", &id, &hp, &x, &y, &ow, &n) == 5) {
                o.hp[id] = (int8_t)hp; o.pos[id] = (int8_t)(y * W + x); o.own[id] = (int8_t)ow;
                p += n;
            }
            State s = fromObs(o, 42, 0, nullptr);
            memset(tt, 0, sizeof(TTE) * (gTtMask + 1));
            memset(hist, 0, sizeof hist);
            nodeLimit = CGBENCH;
            think(s, 1e9);
            nn += info.nodes; ms += info.ms;
        }
        fprintf(stderr, "CGBENCH %d.%d ev%d st%d th%d simd%d tt%d q%d %.0f knps\n", rep, c, cfg[c][0], cfg[c][1], cfg[c][2], cfg[c][3], cfg[c][4], cfg[c][5], nn / ms);
    }
    nodeLimit = 0;
    gTtEv = gStaged = gSimd = 1; gThreat = 0; gQtt = 1; gTtMask = (1ULL << TT_BITS) - 1;
    memset(tt, 0, sizeof(TTE) << TT_BITS);
    memset(hist, 0, sizeof hist);
}
#endif

#ifndef CW_LIB
int main() {
#ifdef LOCAL
    if (getenv("CW_GEN")) { runGen(getenv("CW_GEN")); return 0; }
#if NN_H
    if (const char* path = getenv("CW_EVALCHECK")) {   // print the net's evaluation of the first records of a data file
        initNet();
        FILE* f = fopen(path, "rb");
        Rec r;
        for (int k = 0; k < 2000 && fread(&r, sizeof r, 1, f) == 1; k++) {
            char grid[H][W + 2];
            for (int c = 0; c < NC; c++) grid[c / W][c % W] = (r.walls[c >> 3] >> (c & 7) & 1) ? 'x' : '.';
            initTables(grid);
            Obs o;
            for (int u = 0; u < NU; u++) { o.pos[u] = r.unit[u] & 127; o.hp[u] = (r.unit[u] >> 7) & 15; o.own[u] = (r.unit[u] >> 11) & 3; }
            State s = fromObs(o, 42, r.turn, nullptr);
            for (int u = 0; u < NU; u++) s.own[u] = o.own[u];
            printf("%d\n", nnEval(s));
        }
        return 0;
    }
#endif
#endif
    int myId, w, h;
    char grid[H][W + 2];
    if (scanf("%d", &myId) != 1) return 0;
    auto t0 = chrono::steady_clock::now();
    if (scanf("%d %d", &w, &h) != 2) return 0;
    for (int y = 0; y < H; y++) if (scanf("%14s", grid[y]) != 1) return 0;
    for (int y = 0; y < H; y++) fprintf(stderr, "%s\n", grid[y]);
    initNet();
    initSearch();
#ifdef CGBENCH
    cgBench();
#endif
    initTables(grid);
    double msTurn = MS_TURN, msFirst = MS_FIRST;
#ifdef LOCAL
    if (getenv("CW_NODES")) nodeLimit = atoll(getenv("CW_NODES"));
    if (getenv("CW_MS")) msTurn = atof(getenv("CW_MS"));
    if (getenv("CW_FIRST_MS")) msFirst = atof(getenv("CW_FIRST_MS"));
#endif
    State cur;
    bool have = false, first = true;
    int myTurns = 0;
    for (;;) {
        int n;
        if (scanf("%d", &n) != 1) break;
        if (!first) t0 = chrono::steady_clock::now();
        Obs o;
        memset(&o, 0, sizeof o);
        fprintf(stderr, "#S %d", myTurns * 2 + myId);
        for (int i = 0; i < n; i++) {
            int id, type, hp, x, y, owner;
            if (scanf("%d %d %d %d %d %d", &id, &type, &hp, &x, &y, &owner) != 6) return 0;
            o.hp[id] = (int8_t)hp; o.pos[id] = (int8_t)(y * W + x); o.own[id] = (int8_t)owner;
            fprintf(stderr, " %d,%d,%d,%d,%d", id, hp, x, y, owner);
        }
        int turn = myTurns * 2 + myId;
        bool ok = true;
        State nxt;
        if (!have) {
            if (myId == 0) nxt = fromObs(o, 42, 0, nullptr);
            else ok = syncSecond(o, nxt);
        } else ok = syncFrom(cur, o, nxt);
        if (!ok) nxt = fromObs(o, have ? cur.seed : 42, turn, have ? &cur : nullptr);   // should not happen
        cur = nxt;
        have = true;
        fprintf(stderr, "\n#R %d %u%s\n", turn, cur.seed, ok ? "" : " DESYNC");
        double used = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        Move m = think(cur, (first ? msFirst : msTurn) - used);
        puts(moveStr(cur, m).c_str());
        fflush(stdout);
        fprintf(stderr, "d%d s%d n%lld %.1fms %.0fknps\n", info.depth, info.score, info.nodes, info.ms,
                info.ms > 0 ? info.nodes / info.ms : 0.0);
        apply(cur, m);
        first = false;
        myTurns++;
    }
    return 0;
}
#endif
