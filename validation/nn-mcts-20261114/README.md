# Neural PUCT prototype — 2026-11-14

**Implemented, trained, tested. Not competitive; keep production baseline.**

Actual NN inference now runs at newly expanded search states. Shared state trunk outputs value; cheap action head supplies priors. PUCT selects branches, caches expanded nodes, backs up alternating-perspective values. No rollout-to-terminal required. Exact terminal outcomes override NN. Bounded arenas and deadline checks preserve legal fallback. Each turn starts a fresh tree.

## Pilot result

- 16,578 parameters; 208→64→32 shared trunk, 32-feature action head, scalar value head. FP16 storage, float32 inference. Standalone `candidate.c`: 80,322 bytes.
- 1,000 complete expert-containing games, 999 canonical maps; 139,329 transitions verified against frozen simulator. 104,172 expert-turn positions: 83,744 train / 11,228 validation / 9,200 test. Game outcomes supply side-relative value targets. No augmentation, self-play, search-leaf targets, or deduplication claim.
- Twelve local CPU epochs: 6.8 s. Validation policy CE + value MSE selects epoch 2. Later epochs worsen value validation. Test top-1 imitation 42.13%; deployed value MSE 0.66190, versus zero-value 0.87250 and training-mean 0.86890. Better heldout outcome prediction did not translate into strength.
- Map grouping follows earlier experiments; validation/test are not globally untouched. Screen maps have zero overlap with pilot corpus but were used in earlier experiments.

Equal-48-ms development screen, eight maps × both seats per variant, against frozen production baseline:

| Variant | W/D/L | Score |
|---|---:|---:|
| Neural PUCT, learned value | 0/0/16 | 0% |
| Same PUCT/priors, heuristic value | 1/3/12 | 15.63% |
| Alpha-beta, same learned value | 0/0/16 | 0% |

Four worker processes; same-family exact-RNG simulator, no judge timeout enforcement. Small exploratory screen, not promotion holdout. `screen/summary.json` is authoritative; initial raw log printed degenerate normal intervals at zero wins. Those intervals are invalid and superseded. No confidence interval used for this pilot decision.

**Inference:** current value model is inadequate for search; MCTS also loses badly with heuristic values. This does not isolate every cause or disprove NN-MCTS. Neither training longer on this corpus nor adding GPUs is justified as an automatic remedy. Next experiment should add tactical stabilization and stronger/search-leaf value targets, then test again before scaling self-play.

## Runtime and correctness

100 archived positions, rotating variants, one worker; 48 ms total decision budget:

- Neural PUCT median 5,325.5 simulations, 4,951 NN calls, maximum path depth 12.5; median root-action coverage 100%. Median 48.001 ms; observed max 48.014 ms. One position saturated arena and stopped early.
- Heuristic-value PUCT median 5,080.5 simulations. It still computes shared NN trunk for priors; value head cost not removed.
- Baseline median 245,216 alpha-beta visits; learned-value alpha-beta 12,768. Counts represent different work; MCTS path depth is not completed minimax depth.
- C vs packed PyTorch: exact decoded weights, zero action-ranking disagreements on 100 positions; max logit error 1.91e-6, value error 6.13e-7. `training-check.json` evaluates deployed FP16 weights on every validation/test position.
- ASan/UBSan: backup sign, both-seat forced terminal win, terminal draws/losses, prior normalization, visits, exhausted arenas, expired deadline, legal fallback.
- Existing rules/search/recruitment/component tests pass against candidate engine; original alpha-beta-specific driver assertion calls retained `ab_choose`. These do not establish tactical strength of MCTS.
- Persistent protocol: 12 legal turns. Initial cold executable sample max 237.1 ms included launch/setup; repeat first response 50.85 ms, later max 48.08 ms. No judge timing claim.

`tests.json`, `parity.json`, `throughput.json`, `throughput-raw.json`, `protocol-detail.json`, and `screen/` retain evidence.

## Implementation files

- `baseline.c`: immutable starting snapshot, SHA-256 `96ca86d8855230504e4ff1c733270009d33171c7d9e8092361136b9820ef8641`.
- `pv.h`: feature contract and NN inference. Walls, unit identity/order, ownership/HP/leader, neutral membership, remaining actions, side and RNG-known flag. Numeric RNG state is not NN input; simulator retains exact RNG. Ambiguous RNG uses existing stationary-neutral approximation, including during MCTS. No separate alpha-beta fallback for ambiguous RNG in this pilot.
- `mcts.h`: PUCT, per-edge statistics, node/edge pools, deadline handling. No transposition graph, tree reuse, Dirichlet noise, quiescence protection, or online training yet.
- `prepare.py`, `bridge.py`: archived replay verification and data extraction.
- `train.py`: CPU/CUDA training; optional two-GPU torchrun DDP. Training only imitates expert moves and predicts observed outcomes; not AlphaZero policy iteration yet.
- `export.py`: selects supplied checkpoint, packs FP16, emits standalone C; keeps alpha-beta control (`MCTS_SEARCH=0`, `AB_NEURAL_VALUE=1`). Learned-value alpha-beta disables futility because old score thresholds are incompatible.
- `verify.py`, `check_training.py`, `matches.py`: parity, runtime, deployed metrics, development matches. `matches.py` requires a fresh `screen` output directory; results are not silently resumed across model changes.

Production `bot.c` unchanged. No arena submission, IDE update, or remote training.

## Reproduce

Use Python with NumPy/PyTorch and a C11 compiler. Local run used PyTorch 2.15.0; NumPy/PyTorch imported successfully from existing environment. Fresh output directories required.

```sh
python validation/nn-mcts-20261114/prepare.py --max-games 1000 --out /tmp/mcts-cache
python validation/nn-mcts-20261114/train.py --cache /tmp/mcts-cache --out /tmp/mcts-trained --epochs 12
python validation/nn-mcts-20261114/export.py --checkpoint /tmp/mcts-trained/best.pt
cc -O3 -std=c11 validation/nn-mcts-20261114/test_mcts.c -lm -o /tmp/mcts-test
/tmp/mcts-test
python validation/nn-mcts-20261114/verify.py
```

Export replaces this experiment's candidate and packed weights; preserve experiment artifacts or use a copied experiment directory before retraining. Data preparation requires existing `validation/imitation-1m-20261114` corpus/reconstruction; Kaggle bundle already contains extracted cache.

## Kaggle

`kaggle.ipynb` + `nn-mcts-kaggle.zip` (28,653,671 bytes) ready for private manual upload. Notebook checks bundle hashes, uses torchrun with two GPUs when available, trains, exports, runs C smoke tests, packages results. Bundle includes scripts, frozen engine, feature/search headers and extracted training data.

Notebook code cells syntax-checked locally. **CUDA/DDP execution untested**: local machine has no T4s. GPU scaling may lose to CPU for this small model. No Kaggle account/session accessed; GPU training unnecessary for completed pilot. Returned models still require local parity, tactical tests and fresh equal-time evaluation.
