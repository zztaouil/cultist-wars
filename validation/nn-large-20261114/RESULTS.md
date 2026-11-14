# Results: larger root policy versus BOSS 3

**BOSS 3 remains unsolved. This experiment does not establish a stronger bot.**

Trained and retained a 24,833-parameter 128→128→64→1 policy (5.97× the shipped 4,161 parameters), plus a contemporaneous small-model control. Production and arena submission remain unchanged.

## Matched private BOSS 3 tests

Opponent verified as agent 4611894 in all 16 tests. Eight cases: four historical second-seat replays and four fresh first-seat seeds, identical per variant, alternating execution order. No model/weight tuning after seeing these outcomes.

| Policy | W/D/L | Score | First seat W/D/L | Second seat W/D/L |
|---|---|---:|---|---|
| baseline | 3/0/5 | 37.50% | 2/0/2 | 1/0/3 |
| candidate | 2/4/2 | 50.00% | 2/2/0 | 0/2/2 |

Paired score difference: +12.50 percentage points; approximate 95% interval -23.36 to 48.36. Eight cases are too few for a stable strength estimate. Seat samples use different maps; historical replay cases are not untouched holdout.

| Case | Baseline | Larger policy | Unit-margin change |
|---|---|---|---:|
| seat1-921359954 | L | L | +1 |
| seat0-41713001 | W | W | -2 |
| seat1-921359519 | W | D | -1 |
| seat0-41713002 | L | D | +1 |
| seat1-921345459 | L | D | +3 |
| seat0-41713003 | W | D | -3 |
| seat1-921338908 | L | L | +0 |
| seat0-41713004 | L | W | +7 |

No reported invalid actions, timeout errors or zero-candidate RNG observations in this batch. Internal elapsed maxima are recorded in boss-results.json; they are not judge CPU accounting or proof of timing safety.

## Supplementary local matches

24 fresh canonical wall layouts, both seats, 49 ms: **14W/14D/20L**, 43.75% against the shipped small-policy baseline; paired approximate 95% interval 34.78–52.72%. Exact-RNG same-family local simulator; no judge timeout enforcement. No demonstrated improvement.

## Learning and implementation

| Model | Best epoch | Validation loss | Test top1 | Test top3 |
|---|---:|---:|---:|---:|
| Retrained small control | 71 | 1.62414 | 48.96% | 74.59% |
| Larger policy | 13 | 1.64290 | 48.36% | 73.83% |

The shipped Kaggle small checkpoint previously scored 48.94% test top1. Validation, not test or boss results, selected the larger checkpoint. Reused test maps have appeared in prior research. Larger model overfits after epoch 13; capacity alone did not improve imitation.

- Standalone C source: 99,119 bytes, below the historical 100,000-byte cap.
- FP16 storage, float32 inference; all decoded parameters match Python exactly.
- 4,444 complete historical test groups: zero top1 changes from FP16 storage; C versus packed NumPy max logit error 9.54e-6, zero top1 disagreements.
- Root scoring on 500 archived positions: median 0.148 ms, p95 0.303 ms, max 0.443 ms, including first-call decoding. Local timing, not judge certification.
- Existing full engine regression and ASan/UBSan suites pass. Disabled-policy depth-four results match baseline on 100 positions (value, action, nodes and completed depth).
- 12 persistent protocol turns pass legal-command checks; max round-trip 256.5 ms includes startup. No claim of a safe judge timing margin.

## Decision

Retain this candidate as a completed negative/inconclusive capacity experiment; do not replace the current bot or submit it as a demonstrated improvement. Boss score remains around break-even in this pilot, and local regression estimate is negative. Review all prior experiment families in REVIEW.md.

Next useful learning experiment should improve targets/data or learned evaluation influence, rather than merely adding width to root ordering. Any such change must still win matched BOSS 3 tests—especially second seat—and survive separate maps. Current task trained and tested the larger policy; reliable BOSS 3 superiority was not achieved.

Artifacts: large/ and control/ checkpoints, histories and portable weights; candidate.c; export.json; parity.json; disabled-parity.json; root-cost.json; data-audit.json; match-summary.json; boss-results.json; boss/ raw logs and independent Java reconstruction. Reproduction instructions in README.md.

## User-requested arena trial — 2026-11-14

Submitted exact tested candidate as **41711350** after explicit user instruction.
Source archived at `submissions/41711350.c`; SHA-256
`a722bde9d755960698fe3e3aa3bebe365840e0f23f3d36e4fcb123e2529eb6f1`.
Initial ranking: 0%, in progress. Earlier non-submission statements describe
the experiment decision before this explicit arena-trial request. No final
strength or promotion result yet. See submission.json and submission-progress.json.
