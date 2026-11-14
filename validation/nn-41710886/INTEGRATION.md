# Root policy integration — 41710886

`candidate.c` is a standalone C11 candidate built from frozen `baseline.c`
(archived 41709358, recorded byte-identical to arena 41710886). It embeds the
user-returned Kaggle checkpoint, best epoch 40, seed 17, 4,161 parameters.
`integrate.py` reproduces it directly from `cultist-nn-results.zip`.

The policy scores legal root actions once per choose() invocation, before
iterative deepening, inside the existing 49-ms deadline. Exact training feature
code is copied from features.c. Static action scores enter the network before
history or TT ordering changes. A cached integer rank orders root moves;
transposition-table hint remains first. A root state pointer limits the override
to the root, including aspiration re-searches; it is cleared before return.
Internal search, evaluation, quiescence and pruning retain baseline behavior.
Ordering can affect selective search and completed depth; it does not add a
learned position evaluator. No actions are discarded by the policy.

Float32 weights use round-trip decimal literals. Complete source: 88,240 bytes,
below user-specified 100,000 cap. Quantization is unnecessary for this candidate.
Compile with `-DNN_ENABLE=0` to disable policy inference/ordering.

## Verification

- Standalone C11 optimized build passes.
- Existing engine regression suite passes against candidate, including ASan/UBSan:
  rays, transitions, endgame, minimax, quiescence, neutral RNG, observation tracking.
- C vs NumPy float32 logits: 1,000 complete action groups, maximum absolute error
  9.54e-6, zero top-action differences. This checks exported-weight inference;
  feature implementation is copied unchanged from the dataset extractor.
- Full root precompute (generate, features, inference, ranks), 500 replay states:
  median 0.035 ms, p95 0.080 ms, maximum 0.220 ms. Includes simultaneous match
  worker load on local hardware. Not judge timing certification.
- 12 standalone first-turn protocol responses; diagnostics in protocol.json.

Reproduction: `python3 integrate.py`; `python check.py` requires NumPy;
`python3 bench_root.py`; `python3 protocol_check.py`. Paths for match/check scripts
assume repository root as current directory. Runtime outputs in build/ are local.

## Equal-time pilot

`run_matches.py` runs 40 map IDs, both seats, 49 ms per move, four process workers,
against frozen baseline. Maps use the same canonical-wall hashing rule as
training and must be in the model test split, never training/validation.
Test maps have appeared in earlier project research and imitation evaluation;
they are not a globally unseen project holdout. No parameter tuning on match
results. Detailed logs and final summary retained beside this document.

The C simulator uses exact known neutral RNG; the match harness does not enforce
judge timeout forfeits or reproduce uncertain protocol observations. Results
measure this local same-family matchup, not arena rating or broad opponent
strength. Paired approximate 95% intervals use map-pair outcomes.

Existing bot.c remains untouched. No arena upload, submission, or promotion.

Final pilot: **32 wins / 16 draws / 32 losses**, **50.00%** score over
80 games / 40 map pairs. Paired approximate 95% interval: **42.98–57.02%**.
Candidate seat-0 score 58.75%; seat-1 score 41.25%. No demonstrated gain or
regression. Retain as an experimental integrated candidate, not promoted.
