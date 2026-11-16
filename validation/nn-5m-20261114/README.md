# Five-million-input policy experiment — 2026-11-14

**Better imitation; no demonstrated equal-time playing-strength gain.** Production
`bot.c` remains unchanged. No arena submission or IDE overwrite.

## Selected policy

`wide-group.c`: 128→192→96→1, **43,393 parameters**, **98,790 source bytes**.
Per-16-weight-group signed int8 weights, FP16 scales/biases, one-time decoding,
float32 inference. Selected by lowest **quantized validation cross-entropy**;
test accuracy and match outcomes did not choose the model. `final-selection.json`
records the decision. `candidate.c` retains the faster 24,833-parameter FP16 model.
Both floating-point checkpoints remain in `fine-small/` and `fine-wide/`.

The larger export has better loss/top-3 coverage but slightly lower top-1 accuracy
than the small export. It is the preferred imitation model, not a demonstrated
stronger playing bot. Keep the production baseline for now.

## Imitation results

Same 50,601 validation and 50,291 test positions for every comparison. Original
played positions only; canonical maps separated from training. These heldout map
splits have appeared in earlier research, so they are not globally untouched.
Previous-model row evaluates its original FP32 weights; production stores FP16.

| Model | Validation loss | Test loss | Test top-1 | Test top-3 |
|---|---:|---:|---:|---:|
| Previous model (FP32 evaluation) | 1.66280 | 1.63792 | 47.77% | 73.55% |
| New small (FP32) | 1.58519 | 1.56048 | 48.93% | 75.21% |
| New large (FP32) | 1.57847 | 1.55277 | 48.97% | 75.70% |
| New small (FP16 storage) | 1.58518 | 1.56048 | 48.93% | 75.22% |
| Large, row int8 (rejected) | 1.63206 | 1.61191 | 47.28% | 74.03% |
| Large, group-16 int8 (selected) | 1.58281 | 1.55762 | 48.81% | 75.53% |

Row-wise int8 failed despite the stronger floating-point model. First-layer
weights reached 36.11 while median magnitude was 0.11; broad scales erased small
weights. Group-16 scales recovered most accuracy without exceeding the 100,000
byte source cap. The rejected export and its measurements are retained.

## Equal-time matches

64 canonical maps absent from the expanded dataset and archived map lists,
frozen before final checkpoint selection. Both seats, 49 ms per side, four
workers, no concurrent training. Same 64 maps reused across five exploratory
comparisons: **640 games**, not 640 independent maps. Exact-RNG, same-family
simulator; no judge timeout enforcement. Intervals are approximate paired 95%
intervals, without multiplicity adjustment. Every interval includes 50%.

| Candidate / reference | W/D/L | Score | Paired interval |
|---|---:|---:|---:|
| Small root vs production | 45/38/45 | 50.00% | 45.37–54.63% |
| Large row-int8 root vs production | 45/35/48 | 48.83% | 44.54–53.11% |
| Small tree prior vs small root | 48/31/49 | 49.61% | 44.67–54.55% |
| Large group-int8 root vs production | 47/31/50 | 48.83% | 44.14–53.51% |
| Large group-int8 tree prior vs its root | 45/28/55 | 46.09% | 40.09–52.09% |

## Search-prior hypothesis

`tree.c` and `group-tree.c` add policy ordering at the first opponent ply when
search depth is at least three. A 64-entry state-keyed cache reuses scores across
iterative-deepening passes; reset each turn. The TT move stays first. Inference
checks the deadline between actions. No policy logit is added to position value,
and no NN-based pruning is introduced; existing engine pruning remains active.

The grouped tree source is **99,222 bytes**. On 100 archived positions at 49 ms,
its median visited-node count was 407,296 versus 520,656 for root-only; mean
completed depth was 6.98 versus 7.22. The matches do not support enabling this
extra inference at the current budget. Lower-ranked moves can matter in search,
but this implementation did not improve playing strength.

## Training and provenance

- Audited cache: 5,000,000 training inputs; **1,628,124 distinct tactical boards**.
  Includes 412,135 original played positions, reflections, and 3,371,876
  ID-relabeled variants. ID variants are not additional played boards.
- 40 complete passes across stages: **200M example presentations**, including
  repeated inputs. Warm stage 1; small stage 12; wide stage 8; refinements 12/7.
- Warm-start from the archived 1M model. AdamW; LR 0.0003 then 0.00005;
  plateau halving; checkpoint improvement threshold 0.0001. Initial checkpoints
  remain eligible. Final selected refinement epochs: small 9, large 2.
- Wide initialization expands a preserved earlier small checkpoint, divides
  outgoing weights by replication counts, and adds 0.0001 weight noise.
  `wide-parent.pt`, `wide-init.pt`, and `widen.json` preserve exact provenance.
- CPU training. A small dense-network microbenchmark favored CPU over MPS.
  Contiguous block preloading reduced an observed full-pass time from 257 s to
  128 s; concurrent runs took longer. Refinements reuse the audit only after
  verifying metadata and record checksums.
- `summary.json` records durations and complete metrics; `manifest.json` records
  source/model/report checksums. Original corpus and production source preserved.

## Validation

All three exports checked on **100,892 validation/test positions**: decoded
parameters exact, zero C-versus-packed-NumPy top-1 disagreements. Maximum logit
error ≤3.82e-5. Storage quantization differences are reported separately.

Engine regression and ASan/UBSan suites pass. Disabled-policy parity: 100
positions at depth four. Both tree variants preserve full-width depth-four
values on 100 positions with futility disabled. Differential suite: 720
positions, 21,708 transitions, 24 fixed-depth cases. Every exported variant
passes 12 persistent protocol turns. Protocol maxima include initialization;
local timing is not a judge timing guarantee.

## Reproduce

From repository root, using the locked NumPy/PyTorch environment:

```sh
python validation/nn-5m-20261114/run_evaluation.py
python validation/nn-5m-20261114/export_group.py
python validation/nn-5m-20261114/check_group.py
python validation/nn-5m-20261114/run_group_matches.py
python validation/nn-5m-20261114/summarize.py
```

These export the preserved final models, not an arbitrary latest run. Match tags
resume completed games. `match_guard.py` rejects changed sources/maps/simulator
under an existing tag; use a new tag/output directory for changed experiments.
Initial manifests disclose whether hashes were recorded after a batch started.

Example new refinement from the preserved large parent; output directory must
not already exist:

```sh
python validation/nn-5m-20261114/train.py \
  --cache validation/imitation-5m-20261114/cache --out new-refinement \
  --init validation/nn-5m-20261114/wide/best.pt --hidden 192 96 \
  --lr .00005 --seed 83 --threads 4 --epochs 12 --patience 5 \
  --minutes 45 --reuse-audit
```
