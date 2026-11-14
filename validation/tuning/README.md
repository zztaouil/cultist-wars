# Eval tuning by play — leader bonus removed (2026-11-13)

First change in this project supported by a seat-paired result whose interval
clears 50%. Candidate source = `bot.c` with the flat leader bonus at 0
(`LEADER_BONUS`, default 0; `-DLEADER_BONUS=420` restores the old leaf values).

Baseline = `bot.c` as submitted in 41709358 plus the lazy-Q optimization, i.e.
the pre-change local source, kept here as `pre-leader-bonus.c`
(SHA-256 `ce733367331be5c5b19b5ce68ac7b97f9ada53b681339398edd54358982d8f79`).
Shipped source SHA-256 `cd2897ab87048d84bd3df65ec6d2419b33ba740ec8515c5cb40a190d0787f8cc`.

## Why this candidate

`RESEARCH.md` §7.3: fitted on top of our own depth-6 search value, the
leader-alive residual is about −140 eval points in the midgame and **−470 after
action 100** — the flat +420 is too large late. Independently, the §2.5 pilot
with the bonus at 0 scored 54.2% on 60 games (inside noise then). A leader
cannot shoot; its value is recruitment, which the proximity term already
carries. The race term that the arena fit liked most was tested in §7.4 and
**lost** games, so it is not part of this change.

## Gate

`tests/arena_match.py`: real arena maps (initial states reconstructed by the
Java model, `results/maps.json`, 3,097 maps), equal time both sides, every map
played with the candidate in both seats, resumable. The paired statistic
averages a map's two seats, so the first-seat edge (60/40 locally) does not
inflate the interval. Control (`bot.c` vs `bot.c`, 133 pairs): **49.62%,
CI 46.71–52.54** — brackets 50 as it must. The control pooled 16 pairs at 25 ms
with 117 at 20 ms; both sides always share a budget, so this does not bias it.

## Results — candidate score vs baseline, seat-paired

| Variant | Budget | Map shuffle | Pairs | W/D/L | Score | Paired 95% CI |
|---|---:|---:|---:|---:|---:|---:|
| leader bonus 420 → **0** | 20 ms | 2024 | 240 | 194/123/164 | **53.23%** | 50.85–55.61 |
| leader bonus 420 → **0** | **49 ms** | 7777 (fresh maps) | 296 | 243/131/220 | **51.94%** | 49.67–54.21 |
| pooled, both runs | — | — | **536** | — | **52.52%** | **50.88–54.16** |
| leader bonus → 200 | 20 ms | 2024 | 120 | 96/54/92 | 51.25% | 47.93–54.57 |
| 420 early, 0 after action 100 | 20 ms | 2024 | 120 | 95/53/94 | 50.00% | 46.57–53.43 |
| leader bonus → −250 | 20 ms | 2024 | 120 | 72/67/104 | 43.54% | 39.67–47.41 |

Dose–response is monotone and peaked at 0: −250 → 43.5, **0 → 53.2**, 200 →
51.3, 420 (baseline) → 50.0. The phase-restricted version (late only) gains
nothing, so the mis-set weight is not confined to the endgame, unlike what the
§7.3 residuals suggested.

Pooling mixes two budgets and two disjoint map samples; it is reported because
both runs agree in direction and size, not as a single clean experiment. The
49 ms point estimate fell from 54.2% (148 pairs) to 51.9% (296 pairs) as games
accumulated, so treat **+2 points** as the honest size, not +4.

## Correctness

- `make test`: rays, transitions, endgame, minimax, quiescence, neutral RNG,
  observation tracking. `tests/test_protocol.py`: 12 turns, max local latency
  50.4 ms (first-response startup outlier, as before).
- `make sanitize` (ASan + UBSan): pass.
- `tests/test_differential.py`: 720 positions, 21,708 transitions, 24 fixed-depth
  results identical to submission 41708923. The reference is now built with
  `-DLEADER_BONUS=420`, since leaf values legitimately changed; model, move
  generation, transitions and hash/prepare parity are still compared exactly.
  Without the define it fails by exactly 420 in positions where one leader has
  died — the intended change and nothing else.
- `equiv.py`: `bot_r.c` at default macros == pre-change `bot.c`, 200 fixed-depth
  comparisons (value, action, visits), 0 mismatches.
- `shipped_equiv.py`: shipped `bot.c` == the tested `bot_r.c:LEADERB=0`, 200
  comparisons, 0 mismatches. The source that was measured is the source shipped.

## Not done here

No private boss test and no arena submission were run from this session: the
CodinGame token is outside the folder this session can read. `cg_submit.py`
performs both; see `HANDOFF.md` §7.

## Files

`pre-leader-bonus.c` (frozen baseline), `match_*.jsonl` (raw games, one line per
seat), `equiv.py`, `shipped_equiv.py`, `cg_submit.py`, `results/maps.json` is in
`validation/arena-verify/results/`.

## Private Gold-boss series, 10 seeds (2026-11-13, after submission 41710268)

Seeds 5000–5009, launch order alternated per seed, same seeds for both sources.
`boss-tuned-*.json` = shipped `bot.c` (41710268); `boss-baseline-*.json` =
`pre-leader-bonus.c`, which plays identically to submitted 41709358.

| Source | W/D/L | Score |
|---|---:|---:|
| tuned (leader bonus 0) | 3/3/4 | 45.0% |
| baseline (leader bonus 420) | 5/1/4 | 55.0% |

Paired on seed: unit-margin difference **−0.40** (sd 2.55, 95% CI −1.98 to
+1.18); result difference **−0.10** (CI −0.38 to +0.18). **A tie within noise.**
It neither confirms nor contradicts the +2-point self-play result; separating a
change this size here would take roughly **80 paired boss games**, and the
3-hour test-game cap is 150.

Two limits worth keeping in mind:

- **Every private boss game is seat 0** — the API always places us first. Our
  known weakness is the second seat (0/3 vs the boss in arena games, 69% first-
  seat edge at equal rating), and this series cannot see it at all.
- The boss plays one fixed agent, so this measures one matchup, not the Gold
  field that sets the arena score.

Raw logs: `boss-{tuned,baseline}-500{0..9}.json`.
