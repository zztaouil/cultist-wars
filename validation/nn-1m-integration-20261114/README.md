# Million-position policy integration

User requested wiring the trained model into the best bot. Production `bot.c`
now contains the established 41711135 alpha-beta engine with the million-position
model as its root action-ordering policy. The source outside the policy block is
byte-identical to that frozen baseline; 49 ms budget retained. No arena submission
or IDE overwrite performed by this task.

## Base selection

41711135 is the established small-policy baseline, with a saved completed Gold
#3 / 25.15 result. The previous larger-policy experiment had no demonstrated
strength advantage. Live inspection found 41711458 ranking at 48%, score 24.48;
its source is byte-identical to the prior larger-policy trial 41711350.
41711445 is a separate MCTS experiment, not an established stronger engine.
This selection is evidence-based reuse of the established policy/search baseline,
not proof it is globally strongest among every historical bot.

`baseline.c` freezes 41711135. `incoming-bot.c` preserves the prior production
file. Observed newer submission sources and arena snapshots are retained here.

## Model and export

- Training: exactly 1,000,000 positions (253,182 original teacher turns plus
  746,818 verified reflections), from frozen top-ten cohort.
- Model: 128→128→64→1, 24,833 parameters, validation-selected epoch 36.
- Source weights: `../imitation-1m-20261114/model-blocks/`.
- Storage: little-endian IEEE float16 packed as base64; decode once to float32.
- Inference: float32, once per legal root action; cached ranks guide root ordering.
- Standalone source: **99,119 bytes**, below 100,000-byte cap.
- SHA-256: `c7bf57e54c2a174abe5b4cb7074face043a9fea1efe6f11e570ea4370f0b1a35`.

## Validation

- All 32,382 original test positions checked. Decoded parameters match exactly.
  C vs packed NumPy maximum logit error 1.91e-5, **zero top-1 disagreements**.
- Float16 storage changes top-1 choice on 8/32,382 positions relative to full
  precision. Deployed-model test accuracy: **48.184%** (full precision: 48.187%).
- Disabled-policy depth-four parity: exact value, action, nodes, and depth on
  100 archived positions against frozen baseline.
- `make test`, `make sanitize`, and `python3 tests/test_differential.py` pass.
  Differential checks cover 720 positions, 21,708 transitions, and 24 fixed-depth
  results. Protocol checks cover 12 turns; startup-inclusive local maximum
  round-trip 232.3 ms is not a judge timing guarantee.
- Root scoring on 500 archived positions, rerun after match workers finished:
  median **0.149 ms**, p95 **0.310 ms**, max **0.562 ms**, first decode included.

## Match pilot

24 fresh canonical wall layouts absent from the entire million-position corpus
and known prior map files; both seats; 49 ms per side. New policy vs frozen
41711135: **14W / 14D / 20L**, **43.75%** score. Approximate paired 95% interval:
**33.01–54.49%**. Same-family exact-RNG simulator, no judge timeout enforcement.
This pilot does **not** establish improved playing strength. Integration is
installed as explicitly requested; the previous baseline remains available.

## Reproduce

From repository root, with NumPy, C compiler, and Java available:

```sh
python validation/nn-1m-integration-20261114/export.py
python validation/nn-1m-integration-20261114/check.py
python3 validation/nn-1m-integration-20261114/verify_disabled.py
python3 validation/nn-1m-integration-20261114/local_matches.py
python3 validation/nn-1m-integration-20261114/bench_root.py
make test
make sanitize
python3 tests/test_differential.py
```

Export regenerates isolated `candidate.c`; copy to production only intentionally.
Match files resume completed maps. `integration.json`, `export.json`, `parity.json`,
`disabled-parity.json`, `root-cost.json`, and `match-summary.json` retain evidence.
