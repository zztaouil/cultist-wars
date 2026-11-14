# Larger NN policy targeting BOSS 3

Experiment review: [REVIEW.md](REVIEW.md). Results: [RESULTS.md](RESULTS.md).
All artifacts are isolated; `baseline.c` freezes actual submission 41711135's
small root-policy source. `candidate.c` replaces only that policy payload with
a 128→128→64→1 network. No learned value function or policy pruning is added.

## Reproduce

Python 3.12, NumPy/PyTorch (exact installed versions in requirements-lock.txt),
C compiler and Java. From the repository root:

```sh
python validation/nn-large-20261114/train.py --out new-large --hidden 128 64
python validation/nn-large-20261114/train.py --out new-control --hidden 32
python validation/nn-large-20261114/export.py
python validation/nn-large-20261114/check.py
python validation/nn-large-20261114/verify_disabled.py
python validation/nn-large-20261114/bench_root.py
python validation/nn-large-20261114/local_matches.py
```

Training reads the existing `validation/nn-41710886/cache/` without changing it.
New output directories must not already exist. Export/check deliberately use the
preserved `large/` run, not whichever run finished most recently. Checkpoint
selection uses validation loss; test is evaluated after selection. The test map
split has appeared in previous project research.

`boss.py` resumes the declared 16 private tests, reading the existing CodinGame
token locally. It checks live quota before each new test, verifies BOSS 3 and our
seat, rotates variant order, and stops on errors. It never submits or changes
the IDE. Run `summarize.py` and `analyze_boss.py` after the batch. Analysis uses
the independent supplied Java model to reconstruct played actions and verify
final scores; it is not independent proof of the C engine's timing behavior.

`local_matches.py` generates 24 canonical-wall-distinct maps absent from cached
NN records and known saved map lists, using the Java map generator. Each map
plays both seats at 49 ms. Exact neutral RNG is known in this local simulator;
judge timeouts and protocol uncertainty are not reproduced. This is a small
same-family regression pilot, supplementary to the matched boss comparison.

The packed source stores IEEE binary16 weights in base64, decodes once into
float32, and performs float32 inference. `parity.json` compares C against both
original float32 and packed-weight NumPy outputs on all 4,444 historical test
positions. `export.json` records source hashes and 100,000-byte size compliance.

## User-requested arena trial — 2026-11-14

Submitted exact tested candidate as **41711350** after explicit user instruction.
Source archived at `submissions/41711350.c`; SHA-256
`a722bde9d755960698fe3e3aa3bebe365840e0f23f3d36e4fcb123e2529eb6f1`.
Initial ranking: 0%, in progress. Earlier non-submission statements describe
the experiment decision before this explicit arena-trial request. No final
strength or promotion result yet. See submission.json and submission-progress.json.
