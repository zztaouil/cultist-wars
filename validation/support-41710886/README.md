# Improvement implementation against arena code 41710886

Baseline: archived 41709358 source, verified byte-identical to arena 41710886. No API tests, arena submission, or IDE mutation in this implementation task. All strength tests are local.

## Implemented search candidate: recruit-q.c

The existing four-ply quiescence search expands quiet replies when a unit faces an immediate kill or enemy conversion. It did not treat a contested neutral's imminent recruitment as an unstable position. It could therefore use stand-pat (the static position without spending an action) while the opponent was one conversion away from taking the contested unit.

`recruitment_threat()` detects a live enemy leader adjacent to a live neutral, with our live leader at wall-BFS distance at most two from the same neutral. Such a node searches all legal replies, including quiet approaches, blocks and WAIT, under the existing bounded quiescence horizon. No extra plies, no blanket recruitment reward, no prohibition on neutral shots, no change to the transition model. `-DRECRUIT_Q=0` disables the feature. Existing `use_threat_q=0` also disables it for historical differential comparison.

This is a selective search hypothesis, not a complete solution to recruitment races. It does not model contested neutrals farther away or recruitment after the four-ply extension expires. Stand-pat remains heuristic in other positions. The original leader bonus +420, evaluation, RNG tracker, deadline checks and **49-ms production budget** are preserved.

## Rejected implementation: candidate.c

`candidate.c` is the **rejected firing-support experiment**, not the next-submission file. It rewards actual damaging lanes from the best two shooters, accounting for blockers and indirect shots. Two weights were screened at 20 ms on the same 24 historical arena maps, both seats, against the frozen arena baseline:

| Change | W/D/L | Score | Paired approximate 95% CI |
|---|---:|---:|---:|
| Support weight 12 | 16/6/26 | 39.58% | 29.40–49.77% |
| Support weight 30 | 13/9/26 | 36.46% | 24.67–48.25% |
| Contested recruitment Q | 20/10/18 | 52.08% | 46.25–57.92% |

Reject both support weights. Recruitment Q's development result is inconclusive, not a demonstrated strength gain. No further weight tuning was performed.

## Validation

- Dedicated recruitment detection tests: contested distance, wall detour, ownership, dead leaders, player symmetry.
- Full existing C regression suite compiled against recruit-q.c: rays, legal transitions, minimax, quiescence, endgame, hidden neutral RNG, observation tracking.
- Both suites also pass AddressSanitizer and UndefinedBehaviorSanitizer.
- Historical differential test: 720 states, 21,708 transitions, 24 fixed-depth reference results. The historical search comparison disables all threat-aware Q, as the existing suite requires; it validates compatible model/search behavior, not enabled-feature equivalence.
- Standalone protocol check: 12 legal responses. Max local round-trip 238.7 ms includes first-response startup; not a confirmed judge timeout or proof of a safe judge timing margin. The user-selected budget remains 49 ms.

## Reproducibility and limits

`screen.py` runs the rejected support weights; `screen_recruit.py` runs the recruitment-Q development screen. `holdout.py` uses 40 different game IDs from the existing real-map corpus, 49 ms each side, both seats. Map IDs are disjoint from all 91 games used to form the development pool, but this corpus has appeared in earlier project research: it is fresh to this candidate, not globally unseen project data. Maps and outputs are saved locally.

Four process workers, real arena maps, exact reconstructed RNG, same-family opponent. The runner does not reproduce judge hardware, persistent protocol observation uncertainty, or arbitrary top-opponent strategies, and does not enforce timeout forfeits. Paired intervals are normal approximations over map pairs. A few points in these samples cannot establish arena improvement.

`pre-task-bot.c` preserves the incoming local source, which differs from the live arena code. `sources.json` records original artifact hashes. No existing experimental files were overwritten.

## Final full-budget result and delivery

49-ms holdout: **32W / 17D / 31L**, 50.62%, 40 map pairs, paired approximate 95% interval **42.12–59.13%**. Candidate score by seat: P0 58.75%, P1 42.50%. This is inconclusive; no established gain or regression. It does not justify calling the candidate stronger or promising promotion.

**`next-submission.c` is the standalone, tested recruitment-Q candidate**, byte-identical to `recruit-q.c`. `next-submission.patch` shows its only changes against the actual arena baseline. Existing `bot.c` remains untouched because strength improvement is unproven and its pre-existing tuning should not be silently replaced. The candidate is implemented and ready for a deliberate future arena trial, not automatically promoted or submitted.

71 loss-study positions also exercised at 49 ms: 1 selected actions differ from fresh baseline runs. Timing/horizon changes are confounded; action disagreement is not a strength metric. Full diagnostics in `position-check.jsonl`.
