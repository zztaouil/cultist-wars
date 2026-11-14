# Experiment revision — 2026-11-14

Objective: beat BOSS 3. Larger imitation policy is the next controlled experiment,
not an established remedy. Preserve 49-ms budget and archived evidence.

## Revised conclusions across experiment families

| Family / evidence directory | Result | Decision for this run |
|---|---|---|
| `msz`, `throughput` | Search/representation improvements; exact model checks and fixed-depth speedup. Mean nodes are not strength. | Retain engine foundation. |
| `consistency` | Threat-aware Q, terminal count scoring, bounded history; 17/6/9 local final sample. Actual earlier timeout evidence remains. | Preserve rules/search fixes and 49-ms budget. |
| `lmr` | 4% elapsed saving; combined 18/11/19; boss 1/3 vs baseline 2/2. | Rejected; no generic reduction rerun. |
| `strength` | Hand HP thresholds rejected; arrival term inconclusive. Lazy Q evaluation exact-result speed improvement only. | Do not treat reference search labels as ground truth. |
| `arena-study`, `arena-verify` | 3,097 valid games; static-eval prediction claims corrected by search-value analysis. Large race weights lost heavily; HP tuning unproven. | Reuse verified map-grouped data; avoid fitted static-score claims. |
| `tuning` | Leader bonus zero pooled self-play 52.52%; later arena rating 24.91→23.85, boss series inconclusive. | Self-play success did not establish boss improvement. |
| `race`, `race2` | Recruitment/leader safety and tempo sweeps, no established promoted improvement. Some boss artifacts explicitly marked `_invalid`. | Invalid logs excluded; no blanket neutral-shot ban or race bonus. |
| `losses-41709358` | Midgame recruitment deficits; three of four attractive tactical alternatives failed longer branch checks. | Combat survival and recruitment both matter. |
| `support-41710886` | Firing support 39.58%/36.46%; recruitment Q holdout 50.62%, CI crosses 50. | Neither demonstrated gain. |
| `nn-41710886` | 4,161-param Kaggle model: test top1 48.94%; 40 paired maps 50% score. Historical boss batches combined 4/2/4. | Capacity ablation against actual trained checkpoint, not one-epoch local smoke run. |
| `second-seat-41711135` | NN on/off changes 1/56 timed choices; depth-horizon reversal documented. | Root policy has limited influence; bigger accuracy may not affect play. |
| `horizon-41711135` | Contest holdout 51.56%, uncertain; extension 2.33× fixed-depth nodes. All variants lost two second-seat boss cases. | No promotion; avoid expensive blanket extension. |
| `boss-41709358-20261114`, NN arena/boss diagnostics | Historical specific-seat/opponent samples, not randomized capacity comparisons. | Use contemporary paired boss cases for this candidate. |

Saved completed 41711135 ranking: Gold #3/30, rating 25.15 vs boss 27.42.
Historical snapshot only. Beating boss occasionally differs from reliably beating
its policy or ranking above it for promotion.

## Capacity experiment declared before results

- Same feature-v1 extraction, teacher corpus (aCat/Noobkins/MSz), map/reflection
  split and optimizer family. 50,284 positions; 40,828/5,012/4,444 split.
- Train control 128→32→1 and larger 128→128→64→1, seed 17. Larger network has
  24,833 parameters, 5.97× existing model. Fixed lr .001, AdamW decay .0001,
  batch at most 256, at most 100 epochs, patience 12, 15-minute limit per run.
- Only validation cross-entropy selects checkpoint. Reused historical test split
  remains descriptive, not a new untouched final holdout.
- Preserve all runs, weights, hashes, configs and negative results. No claims
  based on test top1 alone; no new heuristics mixed into capacity comparison.
- Export standalone C candidate using existing exact feature extractor and
  root-only ordering. Quantization needed to meet historical 100,000-byte cap;
  measure its ranking errors and overhead, then verify engine invariants.
- Primary practical gate: matched BOSS 3 cases, rotated variant order, include
  second-seat replays and fresh first-seat seeds. Verify opponent ID 4611894.
  Local both-seat games are supplementary. Small boss samples are pilots,
  not statistical proof or automatic promotion.

Historical reports remain immutable. This document supersedes stale handoff
recommendations only for this new experiment; it does not rewrite old outcomes.

## Additional archived sweep reconciliation

Recomputed summaries from `race2` saved match logs: b420pe50 48.39%
(218 games, 108 complete pairs); s20r 45.82% (239 games, 119 pairs);
s60r 38.70% (239 games, 119 pairs). Unpaired trailing games explain the
difference between game totals and twice the complete-pair count. None supports
promotion. Saved x01–x08 boss screens each contain five games and score 0–40%;
x09 is incomplete (three games, 50%). All observed second-seat cases lost.
x10 and the planned x00 control contain no completed games; do not count absent
controls as losses or infer a causal gain from those sweeps.

The separate `boss-41709358-20261114` batch contains nine completed results:
2W/2D/5L. It is not a ten-game batch or a matched NN-capacity experiment.

## Training-data audit

Teacher training positions are 62.51% Noobkins (25,521), 19.11% MSz (7,804),
18.38% aCat (7,503). Seats are approximately balanced (19,983 first /
20,845 second). This is not a BOSS 3 outcome dataset. Capacity expansion alone
cannot create labels for delayed exchanges or successful counterfactual plans.
The larger model's validation loss bottoms at epoch 13, then rises while train
loss continues falling: evidence of overfitting on this fixed representation/data,
not evidence that more epochs would beat the boss.

Future priority if this candidate fails the boss gate: improve data/targets and
where learning influences decisions before increasing parameter count again.
Any new policy/value target still needs same-seat boss comparisons and untouched
map evaluation. Do not declare a deeper-search action superior without longer
continuation checks; previous experiments repeatedly exposed horizon reversals.
