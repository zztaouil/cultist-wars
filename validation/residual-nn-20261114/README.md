# Tiny residual NN pilot — 2026-11-14

**Completed: 3W/8D/5L (43.75%) against frozen baseline at equal 48 ms. Do not promote.**

Local experiment; production `bot.c` untouched by this experiment. No submission.
Supervised fit improves slightly, but this small match screen demonstrates no
playing-strength gain; median search depth drops by one. Larger training is not
justified by this result alone. A useful next experiment is caching the board-only
correction to recover throughput, followed by stronger teacher targets and a fresh
predeclared paired screen.

Frozen baseline SHA-256: `96ca86d8855230504e4ff1c733270009d33171c7d9e8092361136b9820ef8641`.
This is the current working `bot.c` snapshot, including its existing root policy NN.

## Integration

16 aggregate features → 8 ReLU units → scalar, 144 float parameters. Odd network
`(g(x)-g(-x))/2` ensures ownership reversal negates correction. Signed features use
existing ownership bitsets, cached distances/adjacency and occupancy: unit counts,
HP, centrality, mobility, nearest enemy/neutral proximity, and fragile-unit counts.
No rays, BFS, action feature extraction, or dynamic allocation in NN inference.

Add integer correction clipped to ±150 to existing static evaluation (unit material
is 1,000). Only quiescence stand-pat/horizon evaluation uses this path. Existing
`material_threat` suppresses correction, including at exhausted tactical horizon.
This predicate detects threats against side-to-move; it does not mean no tactical
moves exist. Tactical moves still expand through unchanged quiescence. Terminal
returns precede NN evaluation. Internal-node futility evaluation stays unchanged.
No pruning thresholds, search budget, action ordering, or quiescence depth changed.
The original board evaluation cache stores only original scores; residual results
are not cached. Features use no turn counter, side, or RNG state. NN correction is
converted to side-to-move perspective at return.

`RESIDUAL_ENABLE=0` disables correction before feature extraction or threat check.
Enabled/disabled variants share exactly the same source and 48 ms deadline.
`candidate.c` includes headers; `standalone.c` embeds them (99,745 bytes).

## Training

200 complete archived games: every transition rechecked against frozen simulator;
final unit counts and terminal condition verified. Sample two replay positions per
game and reservoir-sample up to eight quiet evaluation states from each 48 ms search.
Deduplicate states within canonical map. 3,508 accepted positions: 344 observed
replay states, 3,164 search states. Split by canonical reflected map hash:
2,860 train / 278 validation / 370 test, across 163 / 16 / 21 maps.

Teacher: frozen alpha-beta, depth two plus original tactical quiescence (four or
six plies where applicable), full window, 100 ms cap. All labels completed;
terminal-scale labels excluded. Target is teacher score minus existing evaluation,
clipped to ±150. This is a shallow pilot teacher, deeper than static leaf evaluation;
it is not a strong solved-position oracle. All labels use player-zero perspective.
Verified game outcomes add a small (0.05) auxiliary tanh value loss **only** for
observed replay states. Counterfactual search leaves never inherit game outcomes.

CPU AdamW, seed 71, 150 epochs, checkpoint selected by validation clipped-residual
MSE (epoch 13, zero-indexed). No test-based weight or bound tuning.

Test teacher-score MAE: original 446.60 → residual 434.12 (2.8% lower).
Mean absolute test correction 43.42. Small supervised improvement is not a
strength claim. Full metrics/checkpoint/history saved in `training.json`, `model.pt`.

## Validation and limitations

- Disabled correction matches frozen baseline action, value, depth and node count
  on 51 fixed-depth sampled positions.
- Existing rules/minimax, search, recruitment and component suites pass with
  ASan/UBSan. Additional checks cover ±150 bound, ownership/side antisymmetry,
  material-threat bypass and exact terminal bypass.
- C vs PyTorch deployed integer correction agrees exactly on 80 archived positions.
- Serial rotating 80-position equal-48-ms runtime benchmark: median visits
  226,384 disabled → 183,424 enabled (19.0% fewer); median completed depth 7 → 6.
  Median elapsed 48.002 ms both; maximum 48.014 / 48.230 ms. Enabled median
  93,359 NN calls. Node counts also depend on changed evaluation/search paths;
  this is end-to-end throughput, not isolated inference latency.
- Depth-three baseline decision proxy on 24 archived positions: selected action
  reaches reference score in 16/24 disabled, 17/24 enabled; mean nonnegative score
  regret 109.00 / 108.25. This is a shallow tactical-quality proxy, not proof of
  tactical correctness, statistical improvement, or solved optimal play.
- Paired match result: **3 wins / 8 draws / 5 losses, 43.75%**; see
  `matches.json`, `games.json`, `match-maps.json`.
  Eight maps × both seats, one worker, equal 48 ms. Match maps excluded from all
  current training/validation/test maps; maps were used by earlier experiments.
  Same-family exact-RNG simulator, no independent judge timeout enforcement.
  Small development screen; not promotion holdout. Historical data/screen reuse
  means no claim these are globally untouched maps.

## Reproduce

Python environment needs NumPy/PyTorch; C11 compiler with sanitizer support.
Local Python: `/tmp/cultist-big-nn-env/bin/python`.
Run scripts from any directory:

```sh
python validation/residual-nn-20261114/collect.py
python validation/residual-nn-20261114/train.py
python validation/residual-nn-20261114/check.py
python validation/residual-nn-20261114/evaluate.py
python validation/residual-nn-20261114/finalize.py
```

Scripts overwrite this experiment's outputs; copy directory first to preserve a run.
Collection relies on local `imitation-1m-20261114` archived games/reconstruction.
Timed search sampling can change data between reruns; saved `data.npz` and
`records.json` preserve this run. Retraining on saved data uses fixed seed.
