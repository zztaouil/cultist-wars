# Throughput validation — 2026-11-13

Final code uses a 49 ms total-turn deadline beginning after the unit-count read.
Parsing, observation tracking, search, and action-tracker advancement contribute
to the logged `elapsed_us` measurement; it is recorded after flushing stdout.
The deadline is checked every 64 search visits. Initial map setup precedes this
measurement. Local benchmark overrides still time search alone.

## Actual CodinGame tests

Same seed (42), code architecture, language (C), and four-ply quiescence.
Values below average emitted diagnostics from game turns 20 through 119.
Self-play includes both seats; boss tests include our seat only.

| Budget | Opponent | Midgame samples | Mean visits | Median visits | Mean depth | Judge timeouts |
|---|---|---:|---:|---:|---:|---:|
| 48 ms | Self | 100 | 118,529 | 116,096 | 5.75 | 0 |
| 48 ms | Gold boss | 50 | 157,751 | 157,760 | 6.32 | 0 |
| 49 ms | Self | 100 | 179,132 | 176,992 | 6.59 | 0 |
| 49 ms | Gold boss | 50 | 136,310 | 128,800 | 6.26 | 0 |

The final 49 ms samples exceed 120k mean visits in both match types. Self-play
finished 4–4; the Gold boss won 5–4. Maximum logged elapsed time across those
225 turns was 49.071 ms. This is evidence from two games, not a guarantee against
future timeouts or a minimum of 120k on every turn. Only 32 of 50 boss midgame
turns individually exceeded 120k; all 100 self-play midgame turns did.

The large difference between 48 and 49 ms self-play samples cannot be attributed
to the extra millisecond: available judge CPU and diverging search/game positions
also affect throughput. A 48 ms self-play turn logged 54.074 ms wall elapsed
without a judge timeout. Intermediate 35/40 ms runs are retained and include
sub-120k samples. No failing/slow results have been omitted.

Visit counts include transposition hits and repeated positions, but no longer
count the search-to-quiescence boundary twice. They are not unique state counts
or Monte Carlo rollouts. Old baseline counters therefore overcount relative to
this implementation.

## Implementation and correctness

- 160-byte cached state versus 244-byte original state; four-byte units.
- Two-word occupancy bitboards, wall-clipped ray masks, packed cell-to-unit lookup.
- Incremental Zobrist unit updates; RNG/list/turn/side remain in search hashes.
- About 17 KiB of Zobrist tables, precomputed integer evaluation terms.
- Tactical-only quiescence generation, separate quiescence and board-evaluation caches.
- No new selective pruning: legal actions, transitions and evaluation retain baseline semantics.
- GCC O3 requested in source; no hardware-specific instruction-set requirement.
- Exhaustive fast-ray/unit-blocker comparisons, original referee wall tests, minimax,
  neutral RNG, observation tracking, protocol tests, ASan and UBSan passed.

Differential tests: 720 positions, 21,708 legal transitions, 24 depth-four results; incremental caches checked against rebuilding. Fixed-depth local elapsed speedup: 2.78× (not a judge-throughput estimate).

Paired local games at 10 ms: 8 wins, 2 draws, 6 losses against `submissions/41708923.c`. Small regression sample; not an arena rating.

## Reproduce

```sh
make test
make sanitize
python3 tests/test_differential.py
python3 tests/benchmark.py --baseline submissions/41708923.c --pairs 8 --budget-ms 10
python3 tests/summarize_judge.py validation/throughput/test-*-42-4[89]ms.json
```

`bot-48ms.c` preserves the alternate budget. `submission.json` records the final
submission; its matching source is archived under `submissions/`.
`existing-test-921180414.json` was another already-running test reported by the
API; its results are excluded because its source/budget were not established.
