# Simulator hot paths — 2026-11-14

## Arena submission — subsequently requested

User explicitly requested arena submission after local promotion. Accepted as
**41714812**, C, SHA-256 `96ca86d8855230504e4ff1c733270009d33171c7d9e8092361136b9820ef8641`.
Exact source and response archived in `submissions/41714812.c` / `.json`.
Initial progress: 1%, Gold, overall rank 20/401; provisional, no promotion yet.
`arena-submit.json` and `arena-progress-41714812.json` retain responses. IDE untouched.
Earlier no-submission statements below describe the optimization investigation.

## Promotion update — user requested

Neutral bit-select promoted to `bot.c` at the user's explicit request, accepting
its small, corpus-dependent search gain. Production now exactly matches `neutral.c`
(95,667 bytes). All other experiments remain rejected. Rules, RNG consumption,
48 ms budget, NN, evaluation, pruning and horizons are unchanged. The original
investigation decision and measurements below are historical; performance claims
are not strengthened by this promotion. Follow-up logs use `promotion-` prefixes. `make test`, `make sanitize`, 27,431
baseline/candidate transitions and targeted ASan/UBSan checks pass. The unmodified
historical differential still fails at its previously documented evaluation
comparison; no new failure. Existing fixed-depth and match evidence applies to
this byte-identical promoted source.

## Original investigation decision (superseded for neutral selection)

**No production change.** `bot.c` remains byte-identical to `baseline.c` (95,665 bytes,
SHA-256 `078ab8c4f496eb6ab4e2b63cde612bf429ffd29617a655ecc017ba03c573759a`).
The best isolated simulation gain did not establish a repeatable whole-search gain.
All seven candidate sources remain here as rejected experiments. No submission,
IDE upload, commit, evaluation/pruning/horizon change, NN change, or budget change.

`incoming.patch` and `incoming-status.txt` preserve the pre-existing dirty workspace
snapshot; the baseline is the incoming working file, **not Git HEAD**. Historical
repository handoffs describe older configurations; this experiment uses the actual
48 ms source, including its existing safety/recruitment/contest features.

## Methods and corpus

Apple Silicon arm64, Apple clang 21.0.0, `cc -O3 -std=c11 -shared -fPIC`, no native
architecture or fast-math flags. `State` is 160 bytes. One process, sequential C calls;
no concurrent benchmark/test/match workers. Ordinary OS scheduling remained active.

Before changing any candidate, profiled 183 archived positions from 31 reconstructed
arena games, sampling actions 0/20/50/80/110/140 where available. Saved exact walls,
units, RNG, neutral membership, side and remaining turns in `positions.json`.
The final cross-corpus check uses 133 saved positions from older reconstruction and
search archives (`holdout-positions.json`); these are unused by this task's screen,
not globally unseen game data. They include openings, fights and late endgames.

`bench.py` builds each source, resets tables/history for each position, initializes
the unchanged NN weights, and searches with root NN ordering. Initialization of
board tables and decoding weights is outside timing; NN inference is inside.
Fixed depth 5 for screens, 6 for final corpus; 60-second emergency deadline, no
budget stop in any retained fixed-depth sample. It compares value, action, visits,
Q visits, cache hits, cutoffs, pruned count, depth and stop status, not scores alone.

Run order reverses by position and repetition. Five initial screen repetitions,
seven row-selection repetitions, nine final repetitions. JSON retains every sample.
Times below are aggregate whole-search seconds per corpus pass, median and min–max.
Transitions/sec are the separate prepared-state `apply()` microbenchmark: repeated
generated actions on each archived parent, consuming resulting hash/RNG into a
volatile checksum. This diagnostic can benefit from compiler dead-store elimination;
it is **not** the promotion criterion or a measure of game strength. In particular,
the undo row's transition microbenchmark still measures its copy/apply compatibility
wrapper; the real make/unmake comparison is its whole-search measurement.

## Profile, before candidate development

`profile.json` records fine exclusive timestamp instrumentation. Its 206 million
`fast_hit()` wrappers distort runtime substantially; do not use those times as a
speed claim. `coarse-profile.json` repeats the baseline with ray work inside callers,
neutral movement inside simulation, and separate counts instead of ray timestamps.
Coarse exclusive shares (still instrumented, not precise sampling-profiler estimates):

- Simulation/apply: 8.43%; generation: 8.79%.
- Evaluation/cache access: 34.50%; root NN: 2.67%.
- Search residual: 11.10%; Q residual: 31.39%; material-threat detection: 3.11%.
- Residual includes ordering, TT checks, terminals, recursion and instrumentation.
- 205,785,444 ray calls; 38,743,642 multi-blocker cases (18.83%). Those walk
  63,787,914 cells, only **1.65 cells per multi-blocker call** on average.
- 8,161,326 simulated children. State copying is a fraction of the 8.43% simulation
  region, so removing copying alone has a small whole-search ceiling.

The main remaining targets are evaluation (including safety/recruitment ray work)
and Q/search ordering/cache overhead. This task intentionally leaves evaluation,
pruning, horizons and NN untouched. Short multi-blocker walks explain why replacing
ray traversal with more elaborate bit operations did not help.

## Candidate results

Initial screen, 183 positions × 5 runs, depth 5. Every variant visits exactly
8,163,498 nodes per pass (including Q visits).

| Source | Median search s (min–max) | Median M transitions/s | Action / TT entry bytes | Decision |
|---|---:|---:|---:|---|
| baseline | 1.641675 (1.612737–1.680004) | 82.43 | 16 / 40 | retain |
| neutral | 1.636993 (1.625481–1.690481) | 86.08 | 16 / 40 | whole-search gain not robust |
| ray | 1.643199 (1.629105–1.723084) | 82.01 | 16 / 40 | reject |
| metadata | 1.652183 (1.634602–1.673808) | 87.51 | 20 / 40 | reject, search 0.64% slower |
| packed | 1.681420 (1.653753–1.702892) | 84.87 | 12 / 32 | reject, search 2.42% slower |
| undo | 1.698662 (1.691856–1.746223) | 76.87 | 16 / 40 | reject, search 3.47% slower |

Comparing medians from independent run distributions can mislead for small effects:
neutral's **paired per-pass median is 0.62% slower** in this screen, despite the
0.29% improvement in the ratio of medians. It loses in three of five passes.

Second screen (`rows-summary.json`), 183 × 7, same visits/depth:

| Source | Median search s (min–max) | Median M transitions/s |
|---|---:|---:|
| baseline | 1.689831 (1.637468–1.795792) | 80.32 |
| neutral | 1.677643 (1.654602–1.836195) | 84.40 |
| rowray | 1.743649 (1.720934–1.879501) | 77.63 |
| rowneutral | 1.747064 (1.704070–1.897275) | 80.65 |

Both row variants lose in all seven paired passes. Neutral loses in five of seven;
paired median is 0.60% slower. Reject both row variants.

Final cross-corpus confirmation (`holdout-summary.json`), 133 × 9, depth 6:

| Source | Median search s (min–max) | Median M transitions/s | Visits/pass |
|---|---:|---:|---:|
| baseline | 3.613872 (3.519745–3.833189) | 80.32 | 21,386,933 |
| neutral | 3.605968 (3.485907–3.804267) | 83.07 | 21,386,933 |

Neutral simulation throughput increases 3.43%; median whole-search time decreases
only 0.22%. Paired median saving 0.63%, range **−1.35% to +1.80%**, wins six of nine.
This conflicts with both development screens. No stable end-to-end benefit across
corpora; **do not promote on the isolated transition gain**.

## Implementation and correctness audit

- **Undo:** saves up to two touched units plus RNG, neutral membership, key and
  metadata. Reverse `replace_unit()` restores occupancy, occupant nibbles, ownership
  and leaders; side/remaining are reversed. `ready` and `rng_known` never change.
  Same neutral unit may be touched twice, restored in reverse order. Root NN uses
  pointer identity: naïvely mutating one state makes every descendant look like
  root. The first prototype failed visit parity; the retained experiment suspends
  `nn_root` during descent and restores it before parent processing. Restores before
  stopped returns, cutoffs and history updates. Saved-parent copy/apply remains for
  tracker compatibility. Rejected despite correctness because slower.
- **SHOOT metadata:** zero means unknown; generated shots carry victim ID+1.
  Manual four-field actions recompute. NN features can reuse it. `same()` and
  `qmove()` ignore metadata; protocol still prints original unit/aim target.
  TT exact hits refer to the same full state, including RNG/neutral membership.
  Crucially, tracker actions can be applied to different candidate states, so
  `track_action()` clears the cached victim before fan-out. Dedicated test gives
  two candidates different blockers. A larger Action expands move arrays/copies
  even though TT padding keeps Entry at 40 bytes; packing lowers Entry to 32 but
  still loses. Packed layout also cannot be blindly `memcpy`ed into old external
  four-int test buffers. New harness marshals named fields. Neither is promoted.
- **Neutral selection:** clear low bits in mask in ascending ID order, then ctz.
  Never intersect with live units. Dead neutral entries remain in rank selection;
  nonexistent rank consumes selection draw only. Live trapped neutral consumes
  direction draw with bound 1. Conversion removes membership; death does not.
  Corpse blocking and direction ordering remain unchanged.
- **Ray alternative:** enumerate occupied ray bits and choose minimum Manhattan
  distance from shooter; Bresenham advances monotonically along each coordinate,
  hence that order equals traversal order. Wall-clipped ray mask unchanged.
- **Row alternative:** choose first occupied row along shot direction, then first
  occupied x in that row. Handles opposite horizontal/vertical directions and
  low/high bitboard boundary. Exactly matches reference traversal, but slower.

`validate.py`: 1,000 randomized states / 27,431 legal action transitions; all eight
sources agree on generated actions/order and complete resulting units, RNG,
membership, side and remaining turns, both prepared and unprepared. Prepared
results compare incremental hashes/caches against fresh `prepare()`.

`targeted.py`: ASan/UBSan on every source. Per source, 1,059,968 ray comparisons
against the original ordered `bullet_hit()` over random walls/occupancy and every
endpoint pair; all 16,384 membership masks × 12 selection ranks; dead entries,
absent indices, trapped direction draws, corpse-blocked sole exit. Undo compares
made state with copy/apply and restored state byte-for-byte. Metadata compares
cached/manual victim, equality/Q encoding and cross-state tracker application.

9,615 candidate fixed-depth comparisons across the three timing experiments match
baseline score, selected action and every recorded search counter exactly.

## Required validation and existing harness failure

- `make test`: **PASS**, including protocol; max logged local round-trip 50.6 ms.
- `make sanitize`: **PASS** (ASan/UBSan).
- `python3 tests/test_differential.py`: **FAIL**, existing evaluation mismatch at
  `assert before.score(state)==after.score(state)`. Identical failure against frozen
  incoming source (`differential-frozen.py`, log retained). This is not introduced
  by an optimization: production is byte-identical to incoming baseline.
- Historical harness defines `LEADER_BONUS=420`, but incoming source uses
  `LEADER_VALUE` and adds safety/recruitment evaluation plus contest horizons.
  `differential-compatible.py` is a separate validation-only copy: sets historical
  `LEADER_VALUE=420`, disables safety/recruitment/race/defensive/contest additions,
  and retains its existing threat-Q/futility compatibility controls. **PASS**:
  720 states, 21,708 transitions, 24 depth-four scores. Its speed ratio is against
  an old submission and is **not** evidence for any candidate here.
- Original test file and production configuration are preserved. Current-behavior
  differential evidence is the frozen-baseline validation above, not the historical
  compatibility run. No claim that all three unmodified commands pass.

## Equal-budget games

See `matches.jsonl`, `matches-stderr.log`, and `match-summary.json` for the final
neutral-selection experiment against frozen baseline. Both get 48 ms, same eight
new map seeds 86000–86007, both seats, sequential games. The unchanged C simulator
is shared; tracker behavior is active. This is a small related-engine sample, not
an independent referee, arena rating, or strength proof. Selection budget is 48 ms
per `choose()`; this harness does not include tracker cost in that deadline.
Result: **10W/1D/5L**, score 65.625%; mean visits/turn baseline 257,970 vs neutral
257,991, mean completed depths 6.668 vs 6.578. Eight-pair exploratory bootstrap
interval 53.125–81.25% is fragile: five pairs score exactly 50%, only three favor
the candidate. Exact paired sign-flip two-sided p=0.25. **No proven strength gain.**
No local win-rate result overrides the rejected performance gate.

## Reproduction

Run from repository root; all scripts write only inside this experiment directory,
except the explicitly requested `make` targets' normal build outputs. Source-generating
scripts reconstruct candidates from the frozen baseline, never overwrite `bot.c`.
Run serially, not with workers.

```sh
python3 validation/simulator-20261114/experiment.py
python3 validation/simulator-20261114/variants.py
python3 validation/simulator-20261114/bench.py
python3 validation/simulator-20261114/bench.py --names baseline neutral rowray rowneutral --reps 7 --out rows
python3 validation/simulator-20261114/bench.py --names baseline neutral --reps 9 --out holdout --positions holdout-positions.json --depth 6
python3 validation/simulator-20261114/validate.py baseline neutral ray metadata packed undo rowray rowneutral
python3 validation/simulator-20261114/targeted.py baseline neutral ray metadata packed undo rowray rowneutral
python3 validation/simulator-20261114/coarse.py
make test
make sanitize
python3 tests/test_differential.py  # known frozen-baseline failure above
python3 validation/simulator-20261114/differential-compatible.py
python3 validation/simulator-20261114/matches.py --baseline validation/simulator-20261114/baseline.c --candidate validation/simulator-20261114/neutral.c --pairs 8 --seed-start 86000 --budget-ms 48
```

Reproduction prerequisites: local C compiler and Python standard library; no network,
credentials, additional packages, uploads or game API needed.
