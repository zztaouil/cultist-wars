# Reference-derived NNUE training experiment

Built and trained a standalone C++17 bot derived directly from the supplied
`cultist-wars-yahya` engine. This is an attributed adaptation and fine-tuning run,
not a new engine or training from scratch. The root production `bot.cpp`, supplied
reference directory, historical C sources, and earlier experiments are unchanged.

## Architecture and provenance

- Frozen `engine.cpp`: supplied Yahya engine, unchanged. Alpha-beta/PVS search,
  deterministic neutral simulation, incremental quantized NNUE.
- Network: 1,911 sparse features, shared 64-unit transformer for both perspectives,
  clipped squared ReLU, four phase output heads; integer C++ inference.
- `teacher.bin`: supplied `tools/nets/n12a.bin`; the starting weights and self-play
  teacher. Its original large-scale training is inherited, not reproduced here.
- `baseline.cpp`: root production source frozen before this experiment.
- `tools/model.py` and `tools/pack.py`: frozen supplied trainer/model and packer.
  `tools/referee.cpp`: supplied referee with only its engine include path changed.
- New work: reproducible self-play orchestration, map-disjoint fine-tuning,
  quantized checkpoint selection, inference/accumulator checks, packaged bot,
  frozen-opponent comparisons and paired-map uncertainty.

## Training completed

352 new self-play games at 8,000 search nodes per move: 256 training games,
48 validation games, 48 test games. 48,913 positions total: 35,468 / 6,650 / 6,795.
Each shard uses a distinct fixed RNG seed. Whole maps are separated, including
horizontal and vertical reflections; any overlap with an earlier split is removed.
No cross-split overlap was found. Training contains 255 distinct wall layouts.
This separation applies to newly generated data; the inherited teacher's original
training maps are unavailable, so independence from pretraining is not established.

20 epochs, AdamW, learning rate 0.0002 with cosine decay, batches of 1,024,
seed 1411, CPU PyTorch. Target:

```
0.4 * sigmoid(0.7 * final_unit_margin) + 0.6 * sigmoid(search_score / 800)
```

No vertical augmentation because bullet tie-breaking is not exactly symmetric.
First-layer weights clipped to int8 range. Select exported quantized checkpoint
using validation target BCE; evaluate final selected checkpoint on test once.
Epoch 1 won; later epochs overfit this small new sample.

| Metric | Supplied teacher | Fine-tuned candidate |
|---|---:|---:|
| Validation target BCE | 0.488620 | 0.487747 |
| Test target BCE | 0.533512 | 0.531906 |
| Test game-result BCE | 0.424539 | 0.425321 |

Lower target loss did not establish stronger play. Training details and every
epoch are preserved in `training.json` and `train.log`.

## Local playing results

Candidate is A in both archived match files; each map plays both seats.

| Frozen opponent | Games | Candidate W/D/L | Score |
|---|---:|---:|---:|
| Supplied n12a, identical engine, 8,000 nodes/move | 128 | 42 / 21 / 65 | 41.02% |
| Incoming root production, stock time budgets | 64 | 64 / 0 / 0 | 100% |

No protocol faults in either match. Teacher comparison uses seed 441100 and
64 map pairs; paired bootstrap 95% interval is 34.77–47.27%. Fine-tuning regressed
against its teacher despite slightly better held-out target loss. Retain n12a
as the stronger reference; this candidate is not an improvement over n12a.

Production comparison uses seed 541100 and 32 map pairs, with the referee's
49 ms turn limit and 1,000 ms first-turn limit, plus its default 15 ms grace.
Bots retain their stock internal budgets: candidate 44/850 ms (normal/first),
production 49/49 ms. This is **not an equal-compute comparison**. Some games ran
concurrently with the fixed-node match/checks on the same host. Timings include
startup and scheduling; the maximum first-turn-inclusive measurement is 852.2 ms
for candidate, 227.8 ms for production. These results screen local behavior,
not strict judge timing. The all-win bootstrap interval is degenerate and must
not be interpreted as certainty about future games.

`results.json` records summaries, paired intervals, and source/network SHA-256
hashes. Raw per-game records remain in `match-teacher.jsonl` and
`match-production.jsonl`. The root bot was not replaced; no arena submission.

## Validation

- `bot.cpp`: packed standalone submission source, 94,681 Unicode characters;
  compile with C++17. Weights embedded; no runtime files/Python required.
- Exact independent NumPy integer calculation vs C++: 6,795 held-out positions,
  zero error. Embedded and file-loaded weights also match exactly.
- ASan/UBSan: 20,197 legal transitions; incremental accumulators, occupancy,
  square lookup, hash, and evaluation match full rebuilding.
- Persistent protocol smoke: 12 legal responses.
- Root `make` succeeds. Root `make test` passes existing engine/strength/protocol
  suites, then fails compiling the pre-existing `tests/test_neural_value.cpp`:
  `use of undeclared identifier 'VALUE_FEATURES'`. The frozen production source
  lacks that test's feature API. Unrelated production tests were not rewritten.

The local match referee shares the reference engine's simulator. These checks do
not independently prove referee correctness, judge timing, or arena strength.
No account access, judge games, submission, or arena promotion occurred.

## Reproduce

From this directory, with a Python environment containing NumPy and PyTorch
(versions recorded in `requirements-lock.txt`):

```sh
make PYTHON=/path/to/python
make check PYTHON=/path/to/python
make matches PYTHON=/path/to/python
```

The environment used here was `/private/tmp/cultist-big-nn-env/bin/python`.
For another training run, copy this experiment to a new directory and remove only
that copy's generated data and output artifacts. `generate.py` refuses to overwrite
existing shards. Preserve this original run as the comparison baseline.

```sh
make build/gen
python generate.py --train-shards 32 --holdout-shards 6 --games-per-shard 8 --nodes 8000
python train.py --epochs 20
make pack PYTHON=/path/to/python
make check PYTHON=/path/to/python
```

The data generation parameters can scale up locally. No cloud jobs or paid compute
are configured. Next useful experiment: deeper labels and more diverse positions,
with a new fixed holdout and match seed range; do not select models on this test set.

## User-requested submission — 2026-11-14

Submitted exact tested candidate as **41716334** after explicit user instruction.
Source archived at `../../submissions/41716334.cpp`; SHA-256
`3d234182c25bb3d239ab908da02c2c1a1049ce6a6dbc982e87d0c975984194f6`.
API accepted the submission. Initial progress is archived in
`submission-progress.json`; no final arena result established yet.
Earlier no-submission statements describe the initial experiment stage.
