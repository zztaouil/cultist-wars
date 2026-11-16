Arena update: user requested submission; accepted as **41714430**. Initial ranking pending. Earlier no-submission notes below are historical.

# Search work — 2026-11-14

Production `bot.c` now uses the current arena submission 41711656 as its frozen
baseline, with changes confined to tactical-search caching and ordering. Its
network, evaluation, four-ply tactical horizon, and 49 ms budget are unchanged.
The incoming local million-position integration is preserved in `incoming.c`;
its network differs from the newer live submission. No arena submission or IDE
mutation was made.

## Latest-match diagnosis

Read live progress, latest 20 battle entries, current submitted source, and four
complete recent matches. The completed live submission was Gold #4, overall
#23/401, rating 23.96. Raw API records are retained here.

Three losses (921437962, 921437843, 921436446) and one win (921437135) were
reconstructed using the unmodified Java model. All **600 C/Java transitions**
match, including RNG. No timeout or invalid-action loss appeared. Midgame
own-decision indices 10–29 averaged depths 7.75, 6.30, 6.20, and 5.45; tactical
search consumed 82.8%, 85.5%, 87.7%, and 91.0% of visits respectively.

Concrete observations:

- 921437962: our second-seat army led 3–2 at action 60, but finished 3–4;
  opponent obtained remaining neutrals. Healthy leaders survived on both sides.
- 921437843: our leader died by action 60; the opponent's leader remained at
  10 HP and the final count was 1–5.
- 921436446: our first-seat 5–4 advantage at action 30 became 4–5 by action 40;
  eventual elimination. Both recruitment and exchanges matter.

These observations do not prove that a deeper search or a different static
heuristic alone fixes the losses. Earlier boss-position analysis likewise finds
positional recruitment access that the current evaluation undervalues.

## Implemented changes

1. Cache the best tactical move in two padding bytes of the existing 16-byte
   QEntry. Use it for ordering even across tactical depths, but reuse score
   bounds only at the identical tactical depth.
2. Tighten alpha/beta with usable cached tactical bounds, instead of accepting
   only immediate cache cutoffs.
3. Order quiet tactical escapes with the bounded side/role/position history;
   reward successful escapes and penalize earlier quiet failures. All legal
   replies remain available when threatened or near the turn limit.
4. Skip static evaluation when threat handling would discard it anyway.

No reduced-depth moves, extra pruning, truncated tactical horizon, enlarged
cache, canonical state hashing, or partial-iteration selection was promoted.
The `tt` diagnostic now also counts non-cutting tactical bound hits; raw hit
counts are not directly comparable with the old diagnostic.

`efficient.c` is the tested final algorithm; `selected.c` and production add only
three explanatory comments. `production.patch` records the change against the
frozen live source. Production source is **99,681 bytes**, below the 100,000-byte judge cap.

## Experiments and limits

`results.json` contains all completed paired match summaries. Development used
32 saved real maps at 20 ms, both seats. The separate 49 ms map set contains
64 newly generated canonical wall layouts excluded from existing saved map
lists (not claimed absent from every training record). Candidate comparisons
reuse that set and are exploratory; it is not an untouched final statistical
holdout after all comparisons.

Rejected experiments:

- Two-ply Q: 22W/18D/24L, 48.44% over 64 development games.
- Three-ply Q: 28W/12D/24L at 20 ms, but 49W/26D/53L at 49 ms (48.44%).
- Keep exact root improvements from interrupted iterations: 23W/14D/27L,
  46.88%; four selected private replays produced 0W/1D/3L.
- Larger cache: fewer nodes but slower in the preliminary fixed-depth run.
- Canonicalize irrelevant RNG and distant countdown after neutrals disappear:
  49W/28D/51L at 49 ms, 49.22%. Not promoted.

Final tactical cache/history algorithm: 24W/16D/24L in 64 development games
(50.0%). At 49 ms: **27W/14D/23L, 53.12%**, approximate map-paired 95% interval
**47.41–58.84%**. These samples do **not** establish stronger arena play.
Same-family C self-play uses exact RNG and does not enforce judge forfeits.
Multiple match workers ran concurrently; ambient load remains uncontrolled.

Selected private replay comparison (same four seeds/opponents/seats, rerun
baseline): **final 2W/0D/2L vs baseline 1W/0D/3L**, no observed timeout or invalid
output. Four selected games are too few for a strength claim. Original arena
outcomes also differ from rerun outcomes, demonstrating timing sensitivity.

## Verification

- `make test`: rays, transitions, endgame, minimax, tactical search, RNG,
  observation tracker, new cache-bound/horizon tests, 12 protocol turns.
- `make sanitize`: existing and new tests pass ASan/UBSan.
- `python3 tests/test_differential.py`: 720 positions, 21,708 transitions,
  24 compatible fixed-depth values match the historical reference.
- 131 archived positions at depth six with futility disabled: values match
  the current live baseline. Actions can differ on ties. This is a corpus
  check, not a proof that selective search is full-width minimax.
- `performance.json`: five alternating-order repeats on that corpus, run after
  local match workers finish. Median elapsed **1.429177 → 1.380758 s**
  (**3.39% less time**), visits **14,853,934 → 14,538,496** (**2.12% fewer**).
  All 131 values match in every repeat. Timing compares the current live
  baseline, rather than the much older differential-test baseline.
- Protocol startup-inclusive maximum was 244.5 ms locally; private judge games
  showed no timeout. Neither figure guarantees all future turns meet 50 ms.

Reproduce final comparisons with a fresh output tag/directory:

```sh
make test
make sanitize
python3 tests/test_differential.py
python3 validation/search-20261114/measure.py
python3 tests/arena_match.py --a validation/search-20261114/live.c \
  --b validation/search-20261114/efficient.c --tag new-run --out /tmp/cw-new-run \
  --maps validation/search-20261114/holdout-maps.json --maps-used 32 --budget-ms 49
```

Frozen source snapshots, individual match records, private logs, and hashes are
retained. Earlier exploratory generator scripts should not overwrite frozen
snapshots when reproducing results; copy the experiment directory first.
