# Full-state neural evaluation in alpha-beta

`bot.cpp` now defaults to a learned value evaluator. It replaces the hand-written
nonterminal evaluation in alpha-beta/quiescence; child values also rank root
moves. This is a working local experiment, **not a strength promotion or arena
submission**. The first 8-game development screen lost every game to the frozen
incoming C++ bot. Better outcome prediction does not imply stronger search.

## Implementation

- Standalone C++17, 239 → 32 → 16 → 1 ReLU MLP with tanh output; 8,225 parameters.
  FP16 packed storage, float32 inference; approximately 53 KB standalone source.
- Inputs: 91 wall cells; 14 identity-ordered slots with XY, HP, relative ownership,
  leader status and neutral-list membership; remaining actions, absolute side,
  RNG-known flag, relative unit counts; all 31 RNG bits. Dead neutral membership
  survives; dead-unit position/attributes are irrelevant and zeroed. Unknown RNG
  bits are masked. Relative ownership and absolute side encode whose turn it is.
  Tactical geometry is available through the full board, not a separate set of
  engineered tactical features.
- Neural output × 10,000 is the sole nonterminal leaf score. Terminal rules retain
  exact ±1,000,000 / draw scores. No heuristic blend or residual evaluation.
- Iterative deepening, PVS, aspiration, TT, history, tactical quiescence and neutral
  simulation remain. Hash moves keep priority. Old root imitation model is removed.
- Hand-tuned futility is disabled for neural evaluation. Evaluation cache now uses
  the complete state key, including side, horizon, RNG and neutral membership.
  Root scoring checks the deadline between children; an already expired deadline
  returns a legal fallback without starting an iteration.
- `-DNEURAL_VALUE=0` retains the old hand evaluator for tests/ablation, but does not
  restore the old policy network. Use `baseline.cpp` for a faithful comparison.
  `-DNN_ENABLE=0` disables neural root ordering only; neural leaves remain enabled.

## Training and limits

Uses the earlier verified 1,000-game corpus: 104,172 expert-turn positions,
83,744 / 11,228 / 9,200 train/validation/test. Existing canonical-map groups keep
splits disjoint. These splits are reused research data, not globally untouched.
Every archived input matches the old encoder; exact RNG recovered from the same
reconstruction. Targets are side-relative final game outcomes, not hand scores.
Training randomly hides RNG on half the inputs to cover uncertain observation.

Twenty CPU epochs; validation MSE selects epoch 2. Deployed packed-weight test
MSE: **0.599886**, versus **0.872500** for zero prediction. This is outcome
regression, not self-play policy iteration or search-leaf training. The poor
match result demonstrates the current model's inadequacy for playing strength.
More representative search positions and stronger value targets remain future
work; longer training alone is not established as a remedy.

`trained/` retains the first known-RNG-only run. `trained-v2/` is the selected
RNG-masking run. Existing experiments and historical C sources are unchanged.

## Verification

- `make test`: rules, transitions, minimax comparison with neural quiescence,
  quiescence threats/bounds, protocol, ASan/UBSan. Historical evaluator assertions
  also run under `NEURAL_VALUE=0`; neural tests independently cover leaf values,
  full-state cache keys, RNG masking, terminal overrides and expired fallback.
- C++ vs packed PyTorch: 104,172 positions; max error **4.18e-7**. Encoder roundtrip:
  1,074 archived positions. Approximate inference time **3.49 µs** locally.
- 60 in-process decisions: median **49.007 ms**, maximum **49.030 ms**, median depth
  9, median visits 25,656. Local host measurements, no judge timeout guarantee.
  Protocol test includes process launch and initialization; its cold maximum is
  about 245 ms on an earlier run and 51.2 ms on the final run, not a
  steady-state decision measurement.
- First screen: four reused maps × both seats, 49 ms per decision, same-family
  exact-RNG simulator, 0/0/8 W/D/L. `screen-candidate.cpp` freezes that version;
  final version adds an immediate expired-deadline return. `screen-final/`
  records the repeat against the exact final source: 0/0/8 W/D/L. No confidence interval from
  this tiny exploratory sample is claimed; generic harness zero-width intervals
  are not meaningful here. No judge or arena evidence.

## Reproduce

Python environment requires NumPy and PyTorch. Use fresh training output paths:

```sh
python validation/nn-alpha-beta-20261114/train.py --out /tmp/new-value-model
python validation/nn-alpha-beta-20261114/export.py --trained /tmp/new-value-model --output /tmp/candidate.cpp
make
make test
python validation/nn-alpha-beta-20261114/verify.py
python validation/nn-alpha-beta-20261114/timing.py
```

Exporter deliberately uses the frozen baseline plus `value.h`, so inspect any
later production changes before replacing `bot.cpp`. Verification scripts target
the selected `trained-v2` artifact. Reports, checkpoints, feature data, frozen
sources and match logs remain here for review.
