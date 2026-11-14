# Consistency experiments — 2026-11-13

## Starting point and observed games

Incoming `bot.c` preserved byte-for-byte as `baseline.c`; it matches the current
41709130 submission's design. Existing workspace edits were retained.
`progress-before.json`: Gold #3, overall #22, score 25.62; boss score 26.92.
Of 93 completed battles returned in `battles-before.json`, 55 wins, 14 draws,
24 losses. Gold boss subset: 3 wins, 1 draw, 4 losses. Tied position zero is
counted as a draw, not a win.

Latest boss loss, 921191439, ended 4–5. Its diagnostics retained a single RNG
candidate throughout. The observed loss included repeated damage exchanges and
late negative count evaluations; that alone does not isolate a search defect.
Regression fixtures independently demonstrate the old quiescence semantics
could ignore a forced enemy conversion, omit an available quiet escape, and
retain a nonzero static score at a terminal draw.

## Changes

- Bounded quiet history keyed by side, leader/cultist role, origin and destination.
  Reward beta cutoffs, penalize only previously searched quiet failures. Reset on
  map initialization. Order hash first, hostile tactics, quiet history, WAIT,
  friendly shots; no legal action removed.
- Four-ply quiescence searches all legal replies when an immediate enemy kill or
  conversion exists. Threat detection includes indirect shot victims. Stand-pat
  cannot claim the opponent declines that threat. Sacrifices remain legal.
- When the remaining game horizon fits inside quiescence, search actual actions
  through terminal count scoring. Outside those cases stand-pat remains selective.
- Final budget is 49 ms as explicitly requested by the user, checked every 32
  rather than 64 visits. Experimental 45 and 35 ms budgets were tested after
  actual own timeouts at both 49 and 45 ms. The 45 ms failure logged
  69.534 ms wall elapsed despite frequent deadline checks; the 35 ms reserve
  avoided errors in five subsequent tests, but the final user-selected 49 ms
  budget retains approximately 1 ms margin and an observed timeout risk.

Evaluation weights, referee transitions, RNG inference and main-search futility
are unchanged. Threat-aware leaves cost throughput and completed nominal depth;
extra nodes are not themselves the objective. The final four-ply cap still has
horizon effects. Existing futility remains heuristic.

## Local experiments

All games use the existing C simulator and generated symmetric maps, with seats
swapped. These are regression samples, not independent referee validation or Elo.

| Experiment | Opponent | Maps | Budget per bot | W/D/L |
|---|---|---|---|---|
| History only | Incoming source | 0–11 | 10 ms | 12/3/9 |
| History + threats | History only | 0–11 | 10 ms | 9/9/6 |
| History + threats | Incoming source | 100–115 | 35 ms | 11/10/11 |
| Corrected ordering + threats | Earlier threat variant | 0–11 | 10 ms | 11/6/7 |
| Corrected ordering + threats | Incoming source | 200–211 | 35 ms | 10/7/7 |
| Faster detector + same search | Incoming source | 300–311 | 35 ms | 14/5/5 |
| Final user-selected budget | Incoming source | 400–415 | 49 ms | 17/6/9 |

The corrected ordering keeps friendly shots below quiet moves and does not
penalize futility-skipped moves. The first holdout was tied; do not describe all
experiments as improvements. Final holdout uses seeds 200–211, recorded separately
in `final-holdout.jsonl`. Budget overrides mean local equal-time tests do not
measure the 49 ms deployment budget.

## Actual CodinGame replays

Fixtures: 921191439, 921189090, 921187689, 921191061 (Gold boss), and 921190460
(Lv0Sys). Original arena outcomes: four losses and one win across these five.
All replays preserve original seat/opponent/map via `replay_game_id`.

- `history-*.json`: ordering ablation. The 921189090 replay had an **opponent**
  timeout; its apparent win must not be counted as strategy evidence.
- `threat-*.json`: first combined variant. Boss subset 2W/1D/1L; Lv0Sys loss.
- `final-*.json`: corrected ordering at 49 ms. Boss subset 2W/1D/1L;
  **own timeout** against Lv0Sys. Retained rather than discarded.
- `safe-*.json`: 45 ms source and denser deadline checks; one own timeout
  remains. Four other games completed without an error.
- `budget35-*.json`: experimental 35 ms deadline, including repeated timeout fixture.

These replays were selected partly from known losses; improvement on them is not
an unbiased population win-rate estimate. Actual judge CPU availability and
wall-clock search truncation also cause repeated tests to diverge.

## Correctness

`make test` and `make sanitize` cover legal protocol, exhaustive rays, transitions,
minimax, RNG tracking, forced conversion, quiet retreat, indirect lethal threat,
terminal draw, and history bounds/isolation/reset.

`final-differential.json`: 720 positions, 21,708 transitions, 24 depth-four values
against the original pre-optimization engine. The fixed-depth comparison disables
new quiescence semantics, retaining history and disabling futility, to test ordering
without pretending that changed leaf values should match the previous engine.

Reproduce:

```sh
make test
make sanitize
python3 tests/test_differential.py
python3 tests/benchmark.py --baseline validation/consistency/baseline.c --pairs 12 --seed-start 200 --budget-ms 35
python3 tests/summarize_judge.py validation/consistency/safe-*.json
```

All experimental source versions and logs remain here. No performance claim rests
on a single favorable replay. Promotion requires the actual arena to rank above
the Gold boss; local results cannot guarantee Legend.

## Final detector optimization and user-selected budget

The final detector checks enemy conversion adjacency, filters own units to HP≤6,
then skips shooters with no potentially lethal victim before testing all legal
aim points. This avoids constructing a complete tactical action list at every
leaf while preserving indirect shots. It matches complete opponent action
generation on 2,000 randomized positions, plus the explicit blocked-direct-ray
regression. `optimized-*.json` tests this implementation at 35 ms;
`selected49-*.json` tests the final user-requested 49 ms source.

The seeds 200–211 sample scored 56.25%; exploratory paired bootstrap 95% interval
37.5–72.9% (`holdout-uncertainty.json`). This does not establish statistical
superiority. Seeds 300–311 provide another untouched local sample for the faster
detector, in `optimized-holdout.jsonl`. Source hashes identify the final code and
incoming baseline. No evaluation weights were tuned on these matches.

The faster-detector holdout scored 68.75% (14W/5D/5L). See
`optimized-uncertainty.json` for paired-bootstrap uncertainty. Final 49 ms
paired comparison uses seeds 400–415, in `selected49-holdout.jsonl`.

Final 49 ms holdout: **17 wins, 6 draws, 9 losses** over 32 games;
62.5% score. `selected49-uncertainty.json` records a paired-bootstrap interval.
Nominal depth averaged 7.50 versus baseline 7.97; the new quiescence searches
additional real replies. A shallower nominal depth does not imply weaker leaves.

## Submission

Submitted **41709358**, C, with the explicitly requested **49 ms** budget and
32-visit deadline checks. `submissions/41709358.c` matches final `bot.c`.
Final judge retest 921203783 replayed the earlier timeout fixture 921191061:
**4–3 win against Gold boss**, 75 diagnostic turns, no reported errors,
maximum logged wall elapsed 49.097 ms. Earlier timeout results remain above;
one clean retest does not establish a zero-timeout rate.

Initial arena snapshot: **3% complete**, still Gold, promotion not yet confirmed.
`progress-after.json` is provisional, not evidence of final ranking improvement.
The initial rank resets during placement; compare the eventual completed rating
against baseline 25.62 and boss 26.92 rather than this early snapshot.
