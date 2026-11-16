# Top-ten imitation dataset — 2026-11-14

User requested one million board positions from top-ten games, followed by
training, and explicitly approved verified board reflections to reach the target.
`leaderboard.json` freezes the live top-ten cohort. Historical versions of those
players' bots are included; these are not exclusively their current submissions.

## Pipeline

- `collect.py`: resume public replay collection through existing public services,
  retain per-agent battle lists and per-game normalized logs, merge immutable
  historical `nn-41710886/games.jsonl.gz`, deduplicate game IDs. Requests are paced;
  HTTP 429 honors Retry-After. Optional `--reverse` supports a second reader.
- `prepare.py`: independent Java reconstruction, final-score check, every original
  transition checked against frozen C engine. Failed/timeout games excluded.
  Only teacher turns count, irrespective of game outcome. No 32-turn sample cap.
- Generate identity/horizontal/vertical/both reflections for training maps.
  Recompute legal actions and features on each transformed board. Reject reflected
  shots whose actual victim changes under directional ray tie-breaking. Moves and
  conversions retain IDs and geometry; teacher label must match a generated legal
  action. These are geometric training examples, not additional played games.
  Neutral RNG trajectories are not asserted to be reflection-invariant.
- Deduplicate observable board state, fixed unit IDs, side, and remaining turns;
  exclude hidden RNG and zero all dead-unit fields for deduplication.
- Canonical wall layouts, including reflections, use deterministic 80/10/10 map
  buckets. Validation/test contain originals only. Same grouping used in previous
  research: heldout metrics are descriptive, not a globally untouched final test.
- `audit.py`: unique states, legal label bounds, feature size, teacher membership,
  map separation, counts, and original-only heldouts.
- `train.py`: memory-mapped features; 128→128→64→1 action scorer, AdamW, lr .001,
  weight decay .0001, batch 256, seed 17. Shuffle 8,192-position contiguous
  blocks and rows within blocks to bound the memory-mapped working set.
  Maximum 100 epochs, patience 12,
  120-minute soft limit. Checkpoint chosen only by validation cross-entropy.
  Test evaluation happens after selection. All training examples seen each epoch.

## Reproduce

From repository root, with Python containing NumPy/PyTorch plus C compiler/JDK:

```sh
python3 -u validation/imitation-1m-20261114/collect.py
python3 -u validation/imitation-1m-20261114/prepare.py
python validation/imitation-1m-20261114/audit.py
python -u validation/imitation-1m-20261114/train.py --out model-blocks
```

Cache and model output directories must be new. Collection resumes existing files.
Frozen feature implementation and engine come from `../nn-41710886/`.
Training retains portable NPZ/JSON weights and a PyTorch checkpoint.
This experiment does not establish improved arena playing strength.

## Dataset result

784 freshly downloaded replay logs merged with historical games; 3,315 games
in the frozen top-ten cohort. Excluded 65 timeout/empty games. All remaining
3,250 games passed final-score and C/Java transition checks (448,724 transitions).

- Training: **1,000,000** unique positions = **253,182 originals + 746,818 reflections**.
- Validation: **33,797** original positions.
- Test: **32,382** original positions.
- Total: **1,066,179** unique observable states, from **319,361 source game/turns**.
- Canonical map groups: **3,039**, with no train/validation/test overlap.
- Excluded 12,330 reflected shot examples with changed victims, 8 unmatched labels,
  and 2 forced-action examples. No duplicate observable states survived/inflated counts.
- Feature cache: 9,540,068,864 bytes, float32, memory-mapped during training.

`cache/report.json`, `cache/audit.json`, `collection.json`, and per-position
`cache/records.jsonl` retain counts and provenance. The initial fully shuffled
run was stopped for memory pressure before completing an epoch; `model/` is
not a trained result. Completed-run destination is `model-blocks/`.

## Trained model

Completed 48 epochs in 2,178.52 seconds (36.3 minutes); validation-only selection
chose epoch 36, then early stopping fired after 12 non-improving epochs.
Architecture 128→128→64→1, 24,833 parameters.

| Metric | New model | Previous larger policy, same test positions |
|---|---:|---:|
| Test cross-entropy | 1.60556 | 1.71339 |
| Test top-1 | 48.19% | 45.15% |
| Test top-3 | 74.29% | 70.03% |

Validation: loss 1.62893, top-1 47.70%, top-3 73.70%.
Test comprises 32,382 original positions. Comparison is descriptive imitation
accuracy, not demonstrated arena/search strength. Archived policy evaluation did
not influence checkpoint selection or hyperparameters.

Deliverables: `model-blocks/best.pt`, `policy_weights.npz`, `policy_weights.json`,
`report.json`, `history.json`, and `verification.json`. Checkpoint, NPZ, and JSON
weights match exactly; all parameters finite. `manifest.json` records hashes.
