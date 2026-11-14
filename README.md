# Cultist Wars C bot

Latest experiment revision and larger-policy training: [review](validation/nn-large-20261114/REVIEW.md).
BOSS 3 is the target; imitation accuracy and self-play alone do not establish improvement.
New candidate and evidence remain isolated in `validation/nn-large-20261114/`.

Standalone C11 bot for the supplied Java referee. Submit `bot.c` as the C solution.

```sh
make
./bot < game_input.txt
make test
make sanitize
```

## Decision model

Inspired by [MSz's first-place postmortem](https://forum.codingame.com/t/community-puzzle-cultist-wars/98099/54).
The post describes techniques, not source code or exact parameters; this is our
implementation, retaining standalone C11.

Search uses iterative-deepening negamax alpha-beta, principal-variation search,
a 65,536-entry transposition table with hash-move ordering, and widening aspiration
windows. Bounded quiet-move history is keyed by side, leader/cultist role,
origin and destination; successful cutoffs reward the move and penalize only
previously searched quiet failures. Hash moves come first, hostile tactics before
quiet history, and friendly fire last.

A four-ply quiescence search extends conversions and non-friendly shots. When
an opponent can kill or convert a unit immediately, it disables stand-pat and
includes all legal replies (escapes, blockers, counterattacks and WAIT). This
includes indirect bullet victims. Within four actions of the actual game limit,
it searches legal actions through terminal unit-count scoring instead of
accepting a static HP/leader advantage as a draw-breaking score.
Guarded futility pruning skips late quiet cultist moves only at depth-one
null-window nodes, outside the last four turns. Leaders, tactical actions, hash
moves and the first candidate are never skipped. Completed iterations supply the
output; interrupted aspiration re-searches retain the last completed result.

Evaluation is intentionally cheap: unit count, health, and precomputed
obstacle-aware BFS distances to recruitment/combat targets. Expensive leaf
ray/exposure scans were removed. There is no flat leader bonus: `LEADER_BONUS`
is 0, because a leader cannot shoot and its value is recruitment, already
carried by the proximity term; the historical +420 scored 50.0% against 0 over
240 seat-paired real-map pairs ([`validation/tuning/README.md`](validation/tuning/README.md)).
Compile with `-DLEADER_BONUS=420` to restore pre-tuning leaf values. The
remaining parameters are hand-set. No claim of matching MSz's reported depth 16
is made.

Bresenham paths match the Java referee's strict tie comparisons and lower-Y
traversal, including the horizontal-direction convention. Indirect shots may
hit friendlies or neutrals; equivalent shots hitting the same victim are
collapsed. Adjacent movement avoids dependence on distant-target pathfinding.
Leader death alone does not lose: elimination or the 150-action limit decides
by surviving unit count.

## Neutral simulation

The supplied referee uses a separate deterministic 31-bit LCG initialized to 42,
not the random map seed. Simulation includes the neutral phase after every action.
It preserves the referee's ID-ordered list, including dead neutral entries,
conversion removal, absent-index draws, trapped-unit WAIT draws, and corpses
blocking movement until end-of-turn cleanup. RNG state and neutral membership
are included in transposition keys.

Across inputs, all legal opponent action outcomes are matched against the
observed board to track possible RNG states. Player one's first input can have
one or two preceding RNG draws. Prediction is enabled only with one consistent
candidate; ambiguity or a mismatch uses stationary neutrals for search. An
unmatched history disables prediction for the rest of the game. Candidate
tracking still runs while ambiguous and normally resolves on the next input.

## Budget and approximations

Search uses a monotonic 49 ms turn deadline checked every 32 visits. The deadline starts
after reading the unit count, including input parsing and observation tracking.
This leaves approximately 1 ms inside the 50 ms judge limit. Override with:

```sh
make clean
make CFLAGS='-O3 -std=c11 -Wall -Wextra -Wpedantic -DBUDGET_MS=30'
```

Outside immediate material threats and the final-action horizon, quiescence
stand-pat and futility remain selective heuristics: they do not provide
full-width minimax guarantees, especially with neutral movement or zugzwang.
BFS evaluation ignores unit occupancy; action legality and simulation do not.
Diagnostics report depth, advantage, visits, cutoffs, cache hits, quiescence visits,
pruned moves, and RNG candidate count. Each search/quiescence invocation counts
once: entering quiescence no longer counts the boundary twice. Visits still
include transposition hits and repeated positions; they are not unique states or
rollouts and are not directly comparable with MSz's reported state count.

## Throughput optimizations

Units use four bytes. Search states cache two 64-bit occupancy words, ownership
masks, a four-bit-per-cell unit lookup, and an incremental Zobrist key. Only changed
units update those caches. RNG, neutral membership, side and remaining turns
remain in search keys. Position and attribute Zobrist tables occupy about 17 KiB.

Ray masks stop before the first wall. A single occupied ray cell resolves directly;
multiple hits retain exact Bresenham traversal order, including shallow negative
slopes whose linear cell indexes are not monotonic. Distance, adjacency and
integer evaluation terms are precomputed. A board-only evaluation cache excludes
RNG, turn budget and side because its stored score is always player-zero-relative;
terminal checks happen first. All caches reset when initializing another map.

Stable quiescence positions generate only tactical candidates; threatened and
final-horizon positions also generate legal quiet replies. A separate cache retains exact-depth quiescence bounds.
Full search evaluates static positions only when futility pruning needs them.
GCC builds request O3 in source so judge-controlled build flags do not leave the
search unoptimized. The default turn budget is now 49 ms, including observation tracking.
The 48/49 ms judge comparison and limitations are documented in
[`validation/throughput/README.md`](validation/throughput/README.md).

`python3 tests/test_differential.py` compares 720 random positions and 21,708 legal
transitions with `submissions/41708923.c`, verifies incremental caches against
rebuilding them, and checks 24 fixed-depth results against the original engine with the new
quiescence semantics disabled (history ordering remains enabled).
Raw judge logs, local comparisons and measurement definitions live in
`validation/throughput/`. The throughput target concerns sampled midgame averages,
not a guaranteed minimum on every turn; early solved/endgame searches stop sooner.

## Verification

`make test` checks every distinct board endpoint pair against direct referee
bullet traversal with every intermediate unit/wall blocker. It checks transitions,
terminal scoring, quiescence tactics, depth-three search against unpruned minimax
with the same quiescence leaves (futility disabled), neutral RNG edge cases,
RNG-sensitive hashes, two-player observation tracking, and protocol legality.
`make sanitize` runs C tests under AddressSanitizer and UndefinedBehaviorSanitizer.
The supplied Java reference implementation remains unchanged.

Reproduce the paired local comparison with a saved baseline:

```sh
python3 tests/benchmark.py --baseline submissions/41708638.c --pairs 8 --budget-ms 35
```

The initial sample scored **10 wins, 3 draws, 3 losses** over 16 paired games
against the pre-change bot. Mean completed depth was 6.79 versus 6.78; tactical
extensions add up to four plies. These generated symmetric maps and the C simulator
provide a regression sample, not an independent referee or arena rating.
Results and judge test logs are retained under `validation/msz/`.

## Consistency experiments

The latest work preserves the incoming source in
`validation/consistency/baseline.c`. Paired local samples, actual judge replays,
source snapshots, and limitations are documented in
[`validation/consistency/README.md`](validation/consistency/README.md).
Tactical tests cover forced conversion, quiet escape, indirect lethal shots,
terminal count scoring, and bounded/reset history. The benchmark accepts
`--seed-start` to keep evaluation maps separate from development maps.

## Decision-quality validation

The latest isolated experiments and replay diagnosis are documented in
[`validation/strength/README.md`](validation/strength/README.md). Seeded saved games
are reconstructed with the supplied Java model and checked against C transitions.
HP-table and recruitment-arrival candidates remain isolated. The leader-bonus
weight is the one evaluation change promoted to production, on a seat-paired
interval that clears 50% ([`validation/tuning/README.md`](validation/tuning/README.md));
`tests/arena_match.py` is the equal-time gate it had to pass, and its own
baseline-vs-baseline control reads 49.6%. Quiescence now skips static evaluation where threat-aware expansion
would discard its value. Compile with `-DQ_LAZY_EVAL=0` to ablate this optimization.
Fixed-depth parity and equal-time results are reported separately; no arena
submission or established strength/promotion gain is claimed.

## Version control

Keep commits focused on one change. Commit-message format is not enforced.
