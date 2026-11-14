# Decision quality and tactical efficiency — 2026-11-13

Incoming `bot.c` is frozen in `baseline.c` (SHA-256 in `baseline.sha256`).
`incoming.patch` preserves the pre-existing tracked changes. Existing untracked
artifacts were not overwritten. No arena submission or IDE mutation was made.
Private Gold-boss tests were explicitly authorized during this task.

## Evidence and loss diagnosis

`reconstruct.py` compiles the supplied, unmodified Java `model` sources with the
small headless `Replay.java` adapter. `SplittableRandom` reconstructs the map from
the saved seed; the referee's separate RNG starts at 42. Every action is validated
and normalized through Java's `getFinalAction`, followed by its neutral phase and
corpse cleanup. Java states are compared byte-for-byte with C transitions. Visible
move, conversion and damage summaries are checked too. This is independent model
validation, unlike C-vs-C self-play. The adapter does not reproduce judge scheduling,
view rendering or timeout enforcement.

Seventeen saved games, 2,207 transitions matched: two recent Gold-boss losses,
five earlier boss/Lv0Sys tests, ten top-player replays. Exact state snapshots and
provenance are in `replay-positions.json`; reconstruction counts in
`reconstruction.json`. No live replay retrieval is needed to reproduce this.

The diagnostic screen covers every own decision through action 100 in four games
(204 positions), using a 500 ms baseline search. Its `approx_gap` is only a screen:
root depth varies, while child depth is capped at seven. **Do not treat those gaps
as matched-horizon regrets.** `labels.jsonl` subsequently scores actual saved moves,
baseline/candidate depth-five moves, and depth-seven reference moves at the same
six-ply child horizon with fresh caches. Search still shares the baseline evaluation,
futility and bounded quiescence; deeper results are not ground truth.

Fourteen replay fixtures in `corpus.json` include earlier controls, avoiding a corpus
made only of already-lost endpoints. The strongest matched-horizon discrepancies:

| Priority | Failure type / occurrence | Earliest supported example | Evidence / confidence |
|---|---|---|---|
| 1 | Premature exchanges / missed quiet escape: three of four screened games | Lv0Sys action 28: unit 7 shoots 13 instead of moving to (5,4) | 832 reference points; saved line loses unit 7 by action 31. Boss-loss action 60 likewise shoots then dies; escape improves reference by 1,003. High confidence in observed loss, medium in counterfactual superiority. |
| 2 | Indirect-shot target / exchange choice: one game | Boss seed 2000 action 32: unit 6 aims at neutral 9, hits enemy 10 for two; aiming at leader 1 is preferred | 879 reference points; saved exchange loses units 6 and 4. Actual victim differs from aimed unit. Medium confidence. |
| 3 | Recruitment sequencing / single-action opportunity cost: one game | Earlier boss loss action 18: unit 2 moves away/back while leader could move toward recruitment | Moving leader to (3,0) gains 1,006 reference points versus saved move. No proof that occupancy-aware leaf BFS solves it. Medium confidence. |
| 4 | Leader retreat / protecting recruitment capability: one game | Boss seed 2001 action 40: retreat leader vs move cultist 3 to (3,1) | 113 points; later action 54 quiet leader escape gains 477. Fixed leader bonus is a hypothesis, not established cause. Medium-low confidence. |
| — | Late terminal unit count, forced conversion, sacrifice, indirect-shot legality | Existing tactical fixtures retained | No new causal frequency claim from these selected games. |
| — | Hidden RNG | Saved diagnostics checked separately | No evidence justifying scenario aggregation. Top-player logs lack our tracker diagnostics. |

Categories overlap; counts are selected-game incidence, not population rates. The
500 ms screen's boss-2000 action 28 and boss-2001 action 52 warnings do **not** survive
the fixed-depth comparison and are not counted as confirmed failures. Lv0Sys's saved
sample ultimately draws; its local tactical mistake is not mislabeled a game loss.

## Isolated experiments

`candidates.py` derives every candidate from the frozen baseline. No LMR was rerun.

1. **HP tables (`eval-hp.c`, `EVAL_HP=0` ablation).** Preserve full-health value;
   penalize fragile cultists/leaders, with jumps around common lethal-hit thresholds.
   Tables are hand-designed priors, not fitted probabilities. No chosen-action
   change in the 14 replay fixtures. Development: 4W/4D/8L on eight paired maps.
   Two repeated existing midgame entries worsen by four deeper-reference points.
   Private boss seeds 2000–2003: 2W/0D/2L versus baseline 2W/1D/1L.
   **Rejected; production evaluation unchanged.**
2. **Recruitment arrival (`eval-race.c`, `EVAL_RACE=0` ablation).** Wall-BFS distance
   to target equals shortest travel to an adjacent cell plus conversion action for
   stationary targets. Convert to alternating-action arrival times including side
   to move, then add a discounted neutral-arrival margin. Occupancy and future
   neutral motion remain approximations. Side is included in the evaluation-cache
   key; full search keys retain all original state semantics. Development:
   8W/2D/6L. One replay leader-retreat choice gains 113 reference points, but an
   opening fixture loses 1,150 reference points. **Inconclusive and not promoted**;
   neither this small sample nor the shared-eval teacher settles true strength.
3. **Lazy static evaluation in threatened Q nodes (`q-lazy-eval.c`,
   `Q_LAZY_EVAL=0` ablation).** Existing code evaluated a position then discarded
   that value when forced to search actual replies. Detect instability first and
   skip this unused evaluation. No action, extension, stand-pat permission, search
   bound, cache key, deadline or leaf value changes. Exact-depth parity supports
   this optimization independently of noisy win rates.

No fitted evaluation, TT replacement, representation rewrite or selective tactical
scope change was justified. Development seeds 3000–3007 were reused for comparing
isolated evaluation candidates, not presented as holdout. Q development uses
3100–3107. Final local and private-boss seeds are distinct from development.

## Measurement and reproducibility

All paired matches give both bots 49 ms including observation and tracker work,
with seats swapped and first seat alternated across seeds. `stats` retain every
turn's elapsed time, value, action, visits and Q visits. >50 ms is only a local wall
overshoot proxy: this in-process C harness does not enforce referee forfeits.
Development runs include overlapping diagnostic/check activity; final local
holdout runs alone among this task's CPU-intensive jobs. Ambient system load remains
uncontrolled. Bootstrap resamples whole map pairs, 20,000 draws with fixed seed.

Fine-grained Q profiling has timestamp overhead. Regions are evaluation/cache
access, threat detection, generation, ordering and transitions; `q_ns` encloses Q
subtrees at the main/Q boundary. Q residual includes recursion, cache probing,
terminal handling and instrumentation. Region times are not uninstrumented cost
estimates. `eval_cost.py` reports warm-cache and forced-miss microbenchmarks; hot
compiler behavior and timer resolution limit interpretation.

```sh
python3 validation/strength/candidates.py
python3 validation/strength/reconstruct.py
python3 validation/strength/corpus.py
python3 validation/strength/diagnose.py > validation/strength/diagnosis-rerun.jsonl
python3 validation/strength/label.py > validation/strength/labels-rerun.jsonl
python3 validation/strength/counterfactual.py
python3 validation/strength/compare.py fixed --candidate q-lazy-eval.c
python3 validation/strength/compare.py tactical --candidate eval-hp.c
python3 validation/strength/compare.py tactical --candidate eval-race.c
python3 validation/strength/profile.py baseline.c
python3 validation/strength/profile.py q-lazy-eval.c
python3 validation/strength/eval_cost.py
python3 validation/strength/verify.py
python3 validation/strength/check_ablation.py
python3 validation/strength/metrics.py
make test
make sanitize
python3 tests/test_differential.py
python3 validation/strength/compare.py matches --candidate q-lazy-eval.c --seed 4000 --pairs 16
python3 validation/strength/summarize.py hp-development.jsonl race-development.jsonl q-development.jsonl q-holdout.jsonl
```

`private_tests.py` uses the codingame skill's API helper and only private test
endpoints. Existing output files are skipped, not overwritten. Use a fresh prefix
for a rerun; observe returned limits. Source/seed/opponent are explicit, and IDE
options are absent. Boss tests use our seat zero only, so are supplementary rather
than seat-swapped evidence.

## Retained change and measured efficiency

Production `bot.c` matches `q-lazy-eval.c` byte-for-byte. The sole change relative to
this task's frozen baseline is lazy Q evaluation, plus its compile-time ablation.
The original evaluation, 49 ms budget, threat rules and full-state hashes remain.
`q-before-ablation-switch.c` preserves the earlier identical-semantics source used
when private Q tests started; the subsequent default-on compile-time switch does
not alter enabled behavior. Final local holdout uses the production-identical file.

Final clock-free comparison: **177/177** fixture/repeat comparisons match value,
chosen action, total visits, Q visits and stop status at depth six. This is 59
fixture entries over three alternating-order repeats (some existing fixtures
repeat a saved midgame), not 177 independent positions. Both variants visit
85,510,374 nodes. Elapsed: **9.136596 s → 8.805686 s**, **3.62% less**.
Individual repeats save 5.11%, 3.91%, 1.84%; small gains remain hardware/compiler
and workload dependent. Preliminary repeat saved 3.78%; both raw runs retained.

Depth-five detailed profile across 45 existing fixture entries: Q accounts for
97.66% of visits and 95.28% of profiled baseline time. Of total measured baseline
time, eval/cache access is 13.36%, threat detection 6.96%, generation 16.48%,
ordering 13.91%, transitions 9.31%. The Q residual includes recursion, terminal
checks, Q-table probing and timestamp overhead; these were not separated further.
Actual child expansions match at 4,165,719. Instrumented optimized total time is
slightly **slower** (1.009 s vs 0.979 s), despite a smaller eval region; do not use
this timestamp-heavy run as the performance claim. Uninstrumented repeated runs
above establish the measured runtime saving.

Forced-miss evaluation microbenchmarks: baseline 23.85 ns/call, HP tables 23.91,
recruitment 29.59 (+24.1%). Warm-cache results: 0.82 / 0.81 / 1.36 ns/call, heavily
compiler/cache dependent. The retained change reduces unnecessary calls rather
than making evaluation arithmetic cheaper. No speed claim relies on nominal depth.

Eight counterfactual branches (four selected decisions, saved versus alternative,
16 subsequent actions each) also match Java for all **128 transitions**.
`counterfactual.jsonl` records complete action sequences. Continuations use the
baseline depth-five policy for both players, not the real opponent: they validate
legality and expose teacher limitations, not wins against the boss. At the branch
horizon, seed-2000's alternative improves unit counts from 4–5 to 4–4; other count
margins tie, and the Lv0Sys escape still loses that unit under the shallow follow-up.
Thus neither teacher advantage nor a legal retreat guarantees a winning continuation.

Private final boss seeds 4100–4103: retained Q candidate **3W/0D/1L**, baseline
**3W/1D/0L**. Matched score difference −12.5 percentage points; exploratory
seed-bootstrap 95% interval **−75 to +37.5 points**. Zero reported errors/timeouts
in these eight games, 517 diagnostic turns; candidate max logged elapsed 62.076 ms,
baseline 49.048 ms. The judge accepted that logged overshoot; it is still retained
as evidence of scheduling/deadline uncertainty. All 983 diagnostic turns across
all 16 private tests report exactly one RNG candidate. No scenario search is
justified by this sample, and zero timeouts does not establish a safe 1 ms reserve.

## Equal-time local results

| Experiment | Seed pairs | Candidate W/D/L | Score | Paired-bootstrap 95% |
|---|---:|---:|---:|---:|
| HP tables development | 3000–3007 | 4/4/8 | 37.50% | 21.88–53.13% |
| Recruitment development | 3000–3007 | 8/2/6 | 56.25% | 34.38–78.13% |
| Q development | 3100–3107 | 6/1/9 | 40.63% | 28.13–50.00% |
| Q untouched holdout | 4000–4015 | **10/10/12** | **46.88%** | **39.06–54.69%** |

These results **do not establish improved equal-time playing strength**. The
retained change is supported as an exact-result efficiency improvement, not as a
win-rate improvement. Its observed development and holdout point estimates are
below 50%; do not describe them as positive strength evidence. No evaluation
candidate was promoted. Bootstrap precision is limited by few map clusters and
outcomes from the same simulator/policy family.

Final local holdout baseline/candidate: 2/1 >50 ms wall overshoots over 2,343/2,342
turns; maxima 51.450/51.719 ms. Q visit share 79.556%/79.551%; mean completed depth
7.586/7.608 (diagnostic only). Earlier development overshoots and timing conditions
remain in `summary.json`, rather than being silently excluded.


Against the older tactical-only engine (`validation/consistency/baseline.c`),
seeds 4200–4203 with both seats: frozen baseline **3W/3D/2L**, optimized bot
**3W/3D/2L**. Matched map-pair score difference 0, exploratory 95% interval
−18.75 to +18.75 percentage points (`style-delta.json`). This adds an opponent
with different tactical behavior; it remains related code, not broad arena diversity.
Both current variants had two local >50 ms wall events in this supplementary test.
Raw match statistics, including the older opponent's overshoots, are retained.

## Final verification and limitations

- `make test`: exhaustive rays, legal protocol, transition, minimax, forced
  conversion, quiet escape, indirect threats, terminal draw, RNG and history checks
  pass. `make sanitize`: ASan/UBSan pass.
- Existing differential suite passes 720 positions, 21,708 transitions and 24
  compatible depth-four results against its historical reference. Its reported
  speed ratio concerns that historical engine, not this task's frozen baseline.
- Disabled optimization matches baseline value/action/visits/Q visits/stop status
  on all 59 fixture entries at depth five. Enabled comparison is 177/177 above.
- Isolated HP and recruitment candidates also pass existing assertions/sanitizers;
  this does not overcome their decision-quality and strength limitations.
- Final protocol smoke test's max process round-trip was 238.5 ms. Follow-up logs
  isolate first-response startup/setup latency: freshly compiled baseline 256.2 ms,
  warmed candidate 51.4 ms; subsequent responses 49.03–49.08 ms. Startup conditions
  differ, so this is diagnostic, not a comparative benchmark or proof of judge
  timing safety. No budget was changed. Raw original and follow-up logs remain.
- Frozen source hash, preserved incoming test diff, and production/candidate byte
  identity checked. `production-change.patch` isolates this task's source edit
  from pre-existing uncommitted work. `artifact-sha256.txt` records final artifacts.

The implemented improvement is a small, measured reduction in redundant tactical
work. **Equal-time strength remains unproven**, and the observed Q win-rate point
estimates are not favorable. Evaluation hypotheses require broader independent
data and better labels before promotion. Blocked-route occupancy, contextual leader
value, terminal tapering and learned evaluation were not added without adequate
causal evidence. Actual boss tests are only four final seeds and one seat; local
holdout uses synthetic symmetric maps. Teacher searches share horizon/evaluation
bias, and alternate continuations can erase their apparent advantage. No guaranteed
promotion, Elo increase or zero-timeout claim is made.
