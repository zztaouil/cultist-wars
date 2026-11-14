# Conservative LMR experiment

Production `bot.c` is unchanged. Incoming uncommitted work is preserved; `baseline.c`
is its byte-for-byte snapshot, `incoming.patch` captures the incoming tracked diff.
No arena submissions were made. User-authorized private boss tests are recorded below.
The experiment remains isolated here.

## Design

`instrument.py` adds diagnostics to the snapshot. `reduce.py` adds only LMR to that
instrumented source; no killers, ordering, evaluation, transition, RNG, terminal,
quiescence, futility, or budget changes. Generated `experiment.c` is standalone.
`LMR=0` is the default; compile with `-DLMR=1` to enable. The test harness can switch
`use_lmr` at runtime. Clear caches/history before changing modes; the harness uses
separate libraries in games and fresh map initialization in fixed-depth tests.

Reduction eligibility: depth >=3, original null window, fourth or later ordered
candidate, MOVE by a cultist, not TT move, nonterminal score window, and more
remaining actions than nominal depth + four-ply quiescence. Parent immediate
kill/conversion threat excludes all reductions (including evasions). Child immediate
kill/conversion threats and neutral recruitment opportunities exclude reductions.
Screening uses the exact existing indirect-ray threat detector. Parent screening is
lazy and cached within the node. Child screening sees the post-neutral-phase state.
This deliberately also excludes moves preserving an existing threat.

Eligible moves get a one-ply-reduced null-window search. Every result above alpha
gets a full-depth null-window search before normal PVS processing. Fail-lows can be
false negatives; re-search is not a correctness proof. Screens do not recognize
multi-action races, quiet strategic threats, zugzwang, or delayed sacrifices.

## Measurement definitions

Main visits exclude the depth-zero handoff; Q visits include all Q invocations,
including cache returns. Total = main + Q, not unique states. Completed nominal
depth excludes up to four tactical plies; LMR paths may be shorter. `qleft` records
Q visits with 0–4 tactical plies remaining. No separate tactical extension beyond
that existing cap was introduced.

`generated / expanded` gives legal branching; `searched / expanded` gives attempted
children at that remaining main depth, before PVS/reduction re-searches. Main
first-move cutoff rate = `first / beta`, excluding TT cutoffs. TT cutoff counters
separate actual main cutoffs from Q cutoffs, unlike the production `hits` counter.

Timing mode wraps Q subtrees at the main/Q boundary, giving disjoint main/Q time;
threat-detection time is a subset of Q time. Threat-aware expansion counters include
final-horizon unstable nodes as well as immediate material threats. Screening time
is separate LMR-only overhead. Fine-grained timestamps perturb throughput; use the
counter/clock-free efficiency run for elapsed comparisons. PVS subtree visit counts
can overlap nested re-searches. Failed aspiration-attempt visits overlap these too;
these attribution fractions must not be summed as independent work.

25 saved positions cover opening, recruitment, combat, late game and final actions,
from four baseline self-play maps (seeds 500–503, 3 ms fixture generation). Exact
states, walls, neutral membership and RNG are retained in `positions.json`.
These are synthetic local positions, not reconstructed arena boards: retained
arena logs lack the complete initial board. Regenerating timed self-play can change
positions; reuse the saved JSON to reproduce the reported comparison.

20 deeper-reference cases include forced conversion, quiet escape, indirect lethal
shots, recruitment races, sacrifice/blocker setups, final count horizons 1/2/4/6/8,
and saved midgames. Reference is baseline depth 7 + existing Q, compared with depth
5 + Q. Each chosen action is independently scored at reference child depth 6 with
fresh caches. This is a deeper selective reference, not exhaustive minimax.

Matches use existing C simulator and symmetric map generator. Seeds 600–607 are
development; 1000–1015 are independent holdout. Each seed plays both seats, with
first seat alternating by seed. Both variants have identical instrumentation,
clock profiling disabled, and the same 49 ms deadline including observation and
tracker advancement. Counters remain enabled to report work. Production baseline
versus instrumentation-disabled parity is checked separately. Neither bot receives
extra time. Bootstrap resamples whole two-seat seed pairs (20,000 draws, fixed RNG).
Local >50 ms wall events are timeout proxies, not actual judge timeout observations;
local games do not impose forfeits. Input/output and process scheduling are not
fully represented by in-process matches. C simulation is not an independent referee.

## Reproduce

From repository root, using Python standard library and a C11 compiler:

```sh
python3 validation/lmr/instrument.py
python3 validation/lmr/reduce.py
make test
make sanitize
python3 tests/test_differential.py
python3 validation/lmr/test_differential.py
python3 validation/lmr/check.py
cc -O1 -g -std=c11 -fsanitize=address,undefined validation/lmr/test_existing.c -o /tmp/cultist-lmr-existing
/tmp/cultist-lmr-existing
cc -O1 -g -std=c11 -fsanitize=address,undefined validation/lmr/test_guards.c -o /tmp/cultist-lmr-guards
/tmp/cultist-lmr-guards
python3 validation/lmr/run.py profile --depth 5 --timing 1
python3 validation/lmr/run.py profile --depth 6 --timing 1
python3 validation/lmr/run.py profile --depth 0 --timing 1
python3 validation/lmr/efficiency.py
python3 validation/lmr/run.py tactical --depth 5
python3 validation/lmr/run.py matches --pairs 8 --seed 600 --budget 49
python3 validation/lmr/run.py matches --pairs 16 --seed 1000 --budget 49
python3 validation/lmr/summarize.py
```

Redirect outputs to the corresponding `.jsonl` files before running `summarize.py`.
`diagnosis.jsonl` was collected before LMR existed and contains baseline and
instrumented baseline only; subsequent profile commands include LMR as variant 2.
`efficiency.py` uses variant 0 = original baseline, 1 = LMR disabled, 2 = LMR enabled,
all with counters/timing compiled out, alternating order for three repeats.

## Measured results

Baseline hash `82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d`
matches archived submission **41709358**. A live progress read subsequently confirmed
41709358 remains the current submission, Gold, ranking complete. User subsequently
authorized boss tests; these are private tests, not arena submissions.

### Diagnosis before reductions

Depth 5 across 25 saved positions: **2,459,359 total visits**, comprising **68,129
main** and **2,391,230 Q** (97.23%). Profiled elapsed 0.322744 s: Q 0.294356 s
(91.20%), main remainder 0.028388 s. Q threat-check region consumed 11.31% of total
profiled time. Unstable Q: 394,058 expansions and 1,298,517 searched replies.
Q visits with 4/3/2/1/0 plies remaining: 246,665 / 210,489 / 259,140 / 644,818 /
1,030,118. This exposes the real cost behind the nominal depth.

| Remaining main depth | Legal branching per expanded node | Searched children per expanded node | First-move share of main beta cutoffs |
|---|---:|---:|---:|
| 1 | 28.21 | 7.04 | 78.8% |
| 2 | 26.42 | 4.87 | 84.6% |
| 3 | 26.33 | 8.23 | 71.4% |
| 4 | 25.30 | 4.28 | 82.6% |
| 5 | 19.92 | 19.92 | no root cutoff |

30,023 of 37,559 main beta cutoffs (79.93%) occur on first move. Actual TT cutoffs:
19,019 main and 711,711 Q. PVS: 261 retries, 281,730 attributed subtree visits.
Fixed-depth root searches do not use aspiration; iterative 49 ms profiling adds
25 failed baseline aspiration attempts (4.81% of visits) and 526 PVS retries
(4.48% attributed visits). LMR timed sample: 26 aspiration failures (4.98%) and
634 PVS retries (8.28%). Timed samples differ in reached depth and completed work.
These overlapping fractions are diagnostic, not additive percentages.

### Efficiency and tactical safety

At fixed nominal depth 6, LMR reduces visits from **13,545,267 to 12,761,267**
(**5.79%**). 11,661 reductions cause 133 full-depth retries (**1.14%**).
17,492 screen calls take 0.534 ms, **0.035%** of the instrumented LMR run; clock
resolution and timestamp overhead limit accuracy of this tiny measurement.

Three alternating-order, clock/counter-free repeats:

| Variant | Total visits | Total elapsed |
|---|---:|---:|
| Original snapshot | 40,635,801 | 4.655895 s |
| Experiment, LMR disabled | 40,635,801 | 4.440503 s |
| Experiment, LMR enabled | 38,283,801 | 4.262408 s |

LMR's within-experiment elapsed saving is **4.01%**. Original versus disabled timing
difference shows compiler/layout/timing variation; identical visits do not guarantee
identical runtime. No node-budget strength claim is inferred from these numbers.

Disabled experiment matches baseline **value, action, main+Q visit counts and stop
state on 45 positions at depth 5**, plus the 25 original profiling positions.
Existing correctness/protocol, ASan/UBSan, and differential tests pass. Differential
coverage: 720 states, 21,708 transitions, cache rebuild checks, 24 depth-four values.
Same checks run against experimental source. Explicit LMR tests exercise a reduction
and verify PV, depth, horizon, leader, immediate-evasion, neutral-recruitment and
indirect-threat exclusions. Existing test suite also runs with LMR enabled.

Against deeper reference, **no additional action-value regression in 20 cases**;
one saved midgame improves from 13-point regret to zero. Both variants retain
63-point regret in the low-HP recruitment race and smaller reference discrepancies
in several midgames. This small fixture suite cannot establish absence of false
negatives. Sacrifice fixtures are coverage scenarios, not proven forced-sacrifice
solutions. Reference itself retains baseline futility and bounded Q.

### Equal-time strength

All local matches use **49 ms**, swapped seats, counters enabled equally on both
variants, clock profiling off. W/D/L below is LMR against reduction-disabled baseline.

| Sample | Seeds | Games | W/D/L | Score | Paired-bootstrap 95% | Completed depth baseline → LMR |
|---|---|---:|---|---:|---|---|
| Development | 600–607 | 16 | 4/3/9 | 34.38% | 21.88–46.88% | 7.417 → 7.662 |
| Independent holdout | 1000–1015 | 32 | 14/8/10 | 56.25% | 50.00–62.50% | 7.226 → 7.425 |

Combined descriptive result: **18W/11D/19L**, 48.96% score. Do not treat combined
sample as a new independent holdout. Bootstrap intervals are exploratory with few
map clusters; narrow holdout interval reflects the observed paired results, not
proof of broad robustness.

Holdout Q visit share baseline/LMR: **81.03% / 80.95%**. LMR attempts **15,566,681**
reductions, **217,862** full-depth retries (**1.40%**), with 38,369,255 retry subtree
visits. PVS retry attribution grows from 60,644,337 to 87,987,228 visits. Mean visits
per turn fall from 458,698 to 447,306 despite slightly greater nominal depth.

Local >50 ms wall events: baseline **2/2,285** turns (0.088%; max 79.275 ms), LMR
**2/2,283** (0.088%; max 51.311 ms). Development: zero for both across 1,193 turns
each. These are retained scheduling/deadline overshoots, not actual judge forfeits.
Original protocol check's max process round-trip was 51.2 ms. The approximately
1 ms margin remains a risk; no time budget was increased or reduced.

### Decision

**Do not promote LMR.** Small efficiency/depth gain did not produce consistent
strength improvement. Production source stays byte-identical to the incoming
baseline; isolated implementation, fixtures, commands and raw logs remain available
for further ablation. No killers or unrelated pruning added. Q expansion dominates,
but weakening threat-aware leaves merely to raise nominal depth is unsupported.


## Actual Gold boss tests

Eight private CG tests through the codingame skill, matched seeds 2000–2003,
Gold BOSS 3 (agent 4611894), 49 ms unchanged. Baseline is original submission source;
`cg-lmr.c` is `experiment.c` with `LMR=1`, `PROFILE=0` prepended. Profiling counters
and timestamps compile out for the judge. Test order alternates by seed. API fresh
tests place our bot in seat 0, so this supplementary sample is not seat-swapped.
No IDE code/settings or arena submission were changed.

| Seed | Baseline unit score | LMR unit score | Baseline / LMR result |
|---|---|---|---|
| 2000 | 0–5 | 0–6 | L / L |
| 2001 | 1–4 | 0–4 | L / L |
| 2002 | 2–0 | 3–0 | W / W |
| 2003 | 5–0 | 0–3 | W / L |

Baseline **2W/0D/2L**, LMR **1W/0D/3L**. Four seeds are far too few for a strong
strength estimate (Wilson 95% win-rate intervals approximately 15–85% and 4.6–70%).
Different game trajectories and judge scheduling prevent attributing unit-score
margins or average depth differences solely to LMR. Seed 2003 is a practical
regression, despite the tactical fixture suite finding no added action regret.

**Zero reported errors/timeouts in eight games**, 486 diagnostic turns. Maximum
logged elapsed: baseline 49.192 ms, LMR 49.123 ms. Q visit shares: baseline 82.69%,
LMR 84.54%. This does not establish a safe timeout rate for a 1 ms reserve.
Raw logs: `boss-{baseline,lmr}-200{0,1,2,3}.json`; summaries: `boss-summary.jsonl`.
CG state/rules/limits are retained in `cg-*.json`; no credentials are stored here.

Reproduce individual private tests with an existing authorized CG token:

```sh
python3 validation/lmr/cg.py info
python3 validation/lmr/cg.py start --variant baseline --seed 2000 --output rerun-baseline-2000.json
python3 validation/lmr/cg.py start --variant lmr --seed 2000 --output rerun-lmr-2000.json
# If a response is still running, poll its returned ID before starting another:
python3 validation/lmr/cg.py poll --id TEST_GAME_ID --output rerun-baseline-2000.json
python3 tests/summarize_judge.py validation/lmr/boss-*.json
```

Respect returned test limits; the helper exits on HTTP errors rather than retrying
rate-limited requests. Do not run `boss_batch.py` again over existing results.
