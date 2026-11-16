# C++ weakness audit — submission 41714965

Production is now standalone C++17 `bot.cpp`. C source remains a historical baseline.
The user confirmed a **46 ms search budget (4 ms reserve)** after the fresh arena timeout was verified.

## Evidence

Live starting submission 41714812: C, Gold #3/30, overall #22/401, rating 24.86;
BOSS 3 rating 27.02; ranking complete. Latest window: 23 wins, 7 draws, 19 losses,
one pending. These are recent-window results, not the full submission.

Eight latest completed losses downloaded. Seven non-forfeit games reconstruct with
exact final counts using the existing independent Java model. Five of those seven
recruit fewer units; the other two recruit equally or better but lose exchanges.
Game 921589293 explicitly forfeits on `$0 timeout!` at frame 29 (scores -1/0).
Its final unit counts therefore deliberately do not match the forfeit score.
Other observed diagnostic turns reach 56,564 us; that does not establish the
specific cause of the forfeit. No RNG tracking failures after initial observations
in this sample. Saved source inspection plus previous detailed loss audits identify
recruit retention, safe recruitment access, and long conversion cycles as remaining
strategic weaknesses. Loss-only samples cannot establish their overall frequency.

## Retained changes

- 48 → 46 ms deadline; clock checks every 8 rather than 32 visits.
- Add 180 evaluation penalty when the strongest legal incoming shot can kill an
  owned unit immediately. Previously one-shot and two-shot lethal exposure shared
  the same 90-point threshold. Applies to leaders and cultists, including indirect
  bullet victims. This improves the leaf risk signal; it is not a forced-loss proof.
- If no search iteration finishes, use the highest-ranked legal root policy action
  instead of unconditionally WAIT. Completed-search selection remains unchanged.
- Default build and test targets now use C++; `AGENTS.md`/README/CLAUDE.md record
  the user's persistent C++ preference. Historical C targets remain available.

Network bytes, game simulation, RNG inference and existing conversion extension
are unchanged. Evaluation stays board-only, preserving its cache key validity.
No claim that long-cycle parity or safe recruitment planning is solved.

## Rejected experiment

`candidate46.cpp` added a bounded leader option value from reachable neutrals.
24 development games, 12 seat-paired real maps at equal 46 ms: **9W/3D/12L,
43.75%** (approximate paired 95% interval 27.65–59.85%). Rejected.
Without that bonus (`no_options.cpp`): **9W/5D/10L, 47.92%** (40.63–55.20%).
Final source removes the disabled option code; remaining behavior matches this
ablation. Development samples do not demonstrate increased strength.

Three matched private replays (same opponent/seat/map): baseline 48 ms **1W/2L**;
full experimental candidate 46 ms **1W/2L**; retained ablation 46 ms **2W/1L**.
Retained variant flips the matsu7874 material-retention loss, keeps the Miek93
rerun win, still loses to BOSS 3. Original timeout does not reproduce in baseline
rerun. Timing-dependent replays and selected losses are not an unbiased rating test.
All raw logs and elapsed maxima are in `judge-*.json` and `judge-summary.json`.

## Validation and limitations

`make test`: existing rays/transitions/minimax/quiescence/RNG/tracker suites,
Q-cache tests, recruitment and exposure tests, new lethal-boundary and
exhausted-budget fallback regression; all run against C++ with ASan/UBSan.
Strict `-std=c++17 -pedantic-errors` standalone build passes. Source <100,000 bytes.
Protocol legality passes. First invocation after recompilation measured 243 ms
including cold process startup; immediate warm isolated rerun measured 48.3 ms.
Local process timing is not judge timing. Lower deadline reduces timeout risk,
not a guarantee under arbitrary scheduling delays.

Development's earliest games overlapped test compilation. Subsequent ablation
and holdout matches run sequentially without other local engine tests. Local
matches use the existing simulator and exact neutral RNG, not end-to-end stdin
observation tracking. Holdout uses 16 separately shuffled real maps with no ID or
layout overlap with the 12 development maps. Historical model-training overlap
is not excluded. Both local engines receive equal 46 ms; private baseline uses
its original 48 ms. Thus these comparisons answer different questions.

## Reproduce

- `make test`
- `python3 validation/cpp-legend-20261114/analyze.py`
- `python3 validation/cpp-legend-20261114/summarize.py`
- `python3 validation/cpp-legend-20261114/report.py`
- Local holdout: `python3 tests/arena_match.py --a validation/cpp-legend-20261114/baseline.cpp --b bot.cpp --tag holdout --out validation/cpp-legend-20261114 --maps-used 16 --shuffle-seed 735819 --budget-ms 46`

Completed match files are append-only/resumable. Use a new tag for any code change.
Source hashes, map IDs, private-game logs, tests and submission receipt are retained
in this directory. No IDE overwrite.

## Expanded private comparison

Across five matched replay cases, retained behavior scores **2W/1D/2L** versus
baseline **1W/4L**. Three tests use `no_options.cpp`; two use final `bot.cpp`
with disabled experimental code removed. The additional BOSS 3 case changes
baseline 3–6 loss to 5–5 draw; Lv0Sys remains a loss. No tested variant forfeits.
Retained behavior's largest logged elapsed time is **47,221 us**. These five
selected loss replays support deployment, not a population-strength conclusion.

## Final holdout

32 games / 16 disjoint seat-paired maps at equal 46 ms: **14W/4D/14L, 50.00%**;
approximate paired 95% interval **37.35–62.65%**. Seat-zero score 78.12%, seat-one
21.88%; both seats are essential. No demonstrated general playing-strength gain.
Submission is a user-requested C++ deployment with confirmed timeout mitigation,
a targeted exposure signal, and improvement on selected matched judge replays.
Promotion remains an arena outcome, not a claim established by these checks.

## Submission

Accepted **41714965**, 2026-11-14 18:35:51 Casablanca, language **C++** verified
through submission history (`submissions-after.json`). Initial progress is 0%,
ranking pending; its language field still cached C, while current submission
history correctly identifies C++. Archived source: `submissions/41714965.cpp`.
No promotion claimed.
