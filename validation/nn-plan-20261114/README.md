# NN + search plan — experiment run, 2026-11-14 (evening)

Plan: `claude/NN-SEARCH-PLAN-2026-11-14.md` (project doc). All experiments E0–E5 run.
No submission made by this run.

**Context change during the run:** another session submitted **41716334** (C++, derived from
the supplied `cultist-wars-yahya` NNUE engine; see `validation/yahya-nnue-20261114/`).
It reached 35.55 in Gold and was **promoted to Legend at 2026-11-14 22:45 UTC**.
The BOSS 3 gate therefore stopped after 4 games ("Your league has no boss").
Everything below concerns our own engine (41715067 lineage), which is now not the
strongest bot on the account.

## Results

| Exp | Change | Opponent | Budget | Games | Score | Paired 95% CI | Verdict |
|---|---|---|---|---:|---:|---|---|
| E2 | Texel weights v1 (`cand_t1`) | live 41715067 | 20 ms | 253 | **66.8%** | 61.8–71.5 | gain |
| E2 | Texel v1 | live 41715067 | 45 ms | 113 | **63.7%** | 56.2–70.6 | gain |
| E2 | Texel v1, half dose (`cand_t05`) | live 41715067 | 20 ms | 122 | 52.5% | 44.2–59.1 | weaker |
| E2 | Texel v2, result-only target (`cand_t1v2r` = **r1**) | Texel v1 | 20 ms | 127 | **61.0%** | 54.9–67.4 | gain |
| E2 | 1.5× extrapolation (`cand_t15v2`) | Texel v1 | 20 ms | 130 | 16.9% | 10.7–22.9 | reject |
| E2 | r1 | live 41715067 | 45 ms | 55 | **70.9%** | 61.7–79.1 | gain |
| E2 | cross-opponent: live | old 41709358 (C) | 20 ms | 122 | 46.7% | 40.0–54.2 | reference |
| E2 | cross-opponent: r1 | old 41709358 (C) | 20 ms | 124 | **59.7%** | 52.3–66.6 | gain transfers (+13) |
| E2 | iteration 2 refit on r1 games (`cand_r2`) | r1 | 20 ms | 126 | 49.6% | 43.8–55.4 | converged |
| E2 | iteration 2, negative weights kept (`cand_r2n`) | r1 | 20 ms | 125 | 46.4% | 40.5–51.4 | reject |
| E3 | NN 32-16, λ0.5, 5.7k games (`cand_nn_a`) | r1 | 20 ms | 252 | 46.2% | 41.0–50.6 | reject |
| E3 | NN 16-8, CMAX 300 (`cand_nn_b`) | r1 | 20 ms | 125 | 52.0% | 45.6–58.4 | neutral |
| E3 | NN 32-16, pure search distillation (`cand_nn_c`) | r1 | 20 ms | 125 | 41.2% | 34.4–47.0 | reject |
| E3 | NN 16-8, 7.9k games (`cand_nn_d`) | r1 | 20 ms | 120 | 50.8% | 42.5–58.3 | neutral |
| E3 | NN 32-16, 7.9k games (`cand_nn_e`) | r1 | 20 ms | 186 | 53.8% | 48.3–59.2 | not significant |
| E4 | LMR on r1 (`cand_r1_lmr`) | r1 | 20 ms | 252 | 45.4% | 41.3–49.6 | reject |
| E5 | root imitation net off (`NN_ENABLE=0`) | live 41715067 | 20 ms | 123 | 50.0% | 44.5–56.3 | neutral → frees 65k chars |
| G3 | BOSS 3, seeds 41714001–2, first seat | — | 45 ms | 2+2 | live 0W/2L, r1 0W/2D/0L | — | stopped: league has no boss |

All local games: seat-paired, equal time, real arena maps from `maps_gate.json`
(596 maps, md5(id) % 5 == 0). Training used only the 2,501 disjoint maps in `maps_train.json`.
Self-play simulator knows the neutral RNG; it is not the judge.

## E0 — hygiene

- Root `bot.cpp` (value-net experiment, 0W/8L) saved to
  `validation/nn-alpha-beta-20261114/bot-final.cpp`. Root `bot.cpp` now equals
  41715067 (sha 53b76385…). It is **not** the live 41716334 source; the other session kept its
  candidate under `validation/yahya-nnue-20261114/`.
- Forfeit audit, `e0/`: all 99 battles of 41715067. Own timeouts **2/99** (games 921600043,
  921603954). 21 of 7,068 logged turns over 50 ms; max logged 59,845 µs. W/D/L 52/21/26;
  vs BOSS 3 1W/0D/4L. Decision: 45 ms for every candidate here.

## E1 — data factory

`gen.py`: self-play at 10 ms/move, 2–10 random opening plies, every ply recorded with
state, root search value, completed depth, eight eval terms (`ext.h`, verified equal to
`evaluate` on 300 random positions) and the threat flag. 7,909 games / 1.08M positions in
the last training set (`dall3.npz`): 2,942 games of live engine, 330 of Texel v1, the rest
r1. Device-side raw games in `data/`, `data2/`; container-only portions summarized in the npz files
copied here.

## E2 — Texel fit (the win)

`fit_texel.py`: per-regime logistic fit (both leaders + neutrals / one leader + neutrals /
no neutrals), unit weight fixed at 1000, K grid-searched. `mk_texel.py` builds candidates;
alpha 0 reproduces production eval exactly on 800 positions (`eqcheck.py`).

r1 weights (`texel_r1.json`, points per term unit; production was 1000, 18, 1, 1, 1, 1, 1, 1):

| Regime | unit | HP | leader proximity | leader nearest | fighter nearest | safety | recruit access | race |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 leaders, neutrals | 1000 | 60.4 | 8.18 | 3.12 | 1.14 | 2.84 | 9.17 | 9.24 |
| 1 leader, neutrals | 1000 | 58.2 | 0.84 | 2.44 | 1.79 | 3.07 | 7.47 | 3.05 |
| no neutrals | 1000 | 18.6 | 0 | 0 | 2.63 | 2.87 | 0 | 0 |

Production badly undervalued HP (18 → ~60 while neutrals remain) and every
positional/recruitment term (×3–9). This matches the BOSS 3 diagnosis: access to
neutrals was priced at ~10 points. Earlier hand tries failed by raising one term at a time;
the fit raises them together and per regime. A second iteration on r1's own games
converged (49.6%).

r1 = `cand_t1v2r.cpp`, 97,055 chars, root imitation net still present.

## E3 — sparse value-correction net

`train_nn.py`, `mk_nn.py`: 455 sparse (category × cell) one-hot + HP-scaled inputs, side,
remaining actions → 32 (or 16) clipped ReLU → 16 (8) → 1, output CMAX·tanh added to
the r1 eval. Augmented by player swap. Walls excluded (with walls it memorised maps).
int8 per-unit weights, base64; root policy net removed (E5) → 54–75k chars.
C++ vs torch parity on 300 positions: mean error 2.4, max 21.8 points.
Speed: 77% of r1 nodes at 45 ms (pre-gate ≥60% passed).
Validation result-MSE improved 0.0869 → 0.0830, but no variant beat r1 in play
(best 53.8%, CI includes 50). Pure search-value distillation hurt (41%). Verdict: not a
lever at this data scale (≤8k games); E3 stage B (NN self-play iterations) not justified.

## E4 / E5

- E4 LMR (depth ≥3, move ≥4, non-leader quiet moves, off under threat/exposure):
  45.4%, rejected again.
- E5 root imitation net off: 50.0%, neutral. Safe to delete for size.

## Gate G status

Steps 1–2 passed only by r1 (local, vs live and vs an older independent engine).
Step 3 (BOSS 3) cannot run: account is in Legend. Tests (`make test`) not run on r1 because
it is not proposed for deployment: the live 41716334 beat our production 64–0 in the other
session's local match. Nothing was submitted.

## If our engine is ever used again

1. Start from r1 (`cand_t1v2r.cpp`), drop the root net (E5), 45 ms.
2. Gate against 41716334 locally before any submission; arena now ranks Legend.
3. The Texel recipe (per-regime logistic fit on self-play outcomes, then refit on the new
   engine's games) may also tune hand-coded parts of the yahya engine.

## Files

Scripts: `ext.h lib.py gen.py split.py load.py fit_texel.py mk_texel.py eqcheck.py check_terms.py
train_nn.py mk_nn.py nn_parity.py speed.py bossgate.py cg.py`
(`cg.py` reads `~/.cgtoken`; no token stored here).
Candidates: `cand_*.cpp`. Fits: `texel_*.json`. Models: `nn_*.pt`. Matches: `match/`.
BOSS games: `boss/`. Data: `data/`, `data2/` (device-generated JSONL), `dall3.npz` (all 7,909 games).

## Submission (user request, 2026-11-14 22:57 UTC)

User chose to replace live 41716334 with r1. Submitted **41716477** = `#define BUDGET_MS 45` +
`cand_t1v2r.cpp` (97,076 chars, sha256 0c26b380…; strict `-pedantic-errors` build and
12-turn protocol check pass). Archived in `submissions/41716477.cpp`. Ranking started in Legend
(4% at first read). 41716334's source stays archived in `submissions/41716334.cpp` if it needs
resubmitting.
