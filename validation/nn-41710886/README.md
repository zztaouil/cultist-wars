# Kaggle policy pilot — arena 41710886

Frozen baseline.c is archived submissions/41709358.c, recorded byte-identical to
arena 41710886 by validation/support-41710886/README.md. SHA-256:
82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d.

Run `python prepare.py`, then `python train.py --device cuda:0 --minutes 12`.
Requires Python, NumPy, PyTorch, GCC, JDK. No online downloads or game API calls.
Preparation regenerates cached data; training reuses it. Paths resolve relative
to scripts. Default training is one GPU, 80 epochs maximum, patience 8,
12-minute soft limit checked at each epoch boundary. Setup time is included.

Replay corpus: existing 2026-11-13 arena study. Exclude TIMEOUT games; Java
reconstructs final actions. Require final surviving-unit counts to match reported
scores and every C transition to match Java live positions, HP, ownership,
leaders, side, remaining actions, neutral RNG and membership. Ignore dead-unit
position differences. Score matching alone is not the validation criterion.

Teacher players: aCat, Noobkins, MSz; include wins/draws/losses. Deterministic
sample of at most 32 positions per game. No outcome-based filtering. Sampling
is not player-balanced. Legal actions come from frozen baseline. Equivalent
shots map to same actual victim. Forced-action positions excluded.

Group wall layouts and their horizontal/vertical reflections into deterministic
80/10/10 train/validation/test splits. No training augmentation. Splits are new
to this model, not untouched by all prior project research. Validation chooses
checkpoint; test evaluated once after selection.

128 features: 14 fixed referee unit slots × 7 destination-relative geometry,
health, relative ownership, leader indicators; 30 action/global values including
actual shot victim effects, baseline static action order, counts, health sums,
wall-BFS distances, remaining turns and seat. Dead slots zero. Fixed scaling;
no fitted normalization. Features intentionally omit hidden RNG as network input.
Exact implementation in features.c; future C inference must reuse it unchanged.

Model: Linear(128,32), ReLU, Linear(32,1): 4,161 parameters. Cross-entropy over
legal actions, padding masked. AdamW, lr=0.001, weight_decay=0.0001, batch=256.
No policy-based pruning or engine changes yet. Reference static ranking excludes
history/TT and is not a baseline search-strength result. Source file and engine
remain unchanged. Export is float32, not yet quantized or integrated into C.

Outputs: output/report.json, history.json, policy.pt, policy_weights.npz,
policy_weights.json. Return report and weights for integration/parity checks,
root overhead measurements, then paired 49-ms matches. No strength gain claimed.
