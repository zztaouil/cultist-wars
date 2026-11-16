# Component tweaks against latest arena weaknesses

Baseline: submitted 41714601, SHA-256 728cbc8412a487c31da12fa240f6c4bd5e8e3fb99332e02979d9ef017c3a64d0. Production unchanged. This is a component investigation, not a validated strength upgrade.

## Ranked changes

1. **Evaluation: retained material.** Add a bounded danger term for owned units, not just neutrals. Start with strongest reachable enemy shot, remaining HP, escape availability and friendly support; use exact bullet victims including indirect aim paths. Penalize sustained exposure, avoiding a huge immediate-lethal penalty already addressed by search. Keep calculations cheap and cacheable. Audit cases: 921574605 actions 79–93, 921574110 actions 115–120. Current recruit risk disappears at ownership change; current leaf evaluation has no explicit owned-unit exposure. Future multi-action damage remains a heuristic, not a forced loss. Compare this feature independently before combining with recruitment changes.

2. **Evaluation: restore tunable leader value.** Current source hardcodes +420. Prior tuning tested 0 and reported 51.94% at 49 ms with interval 49.67–54.21 (not conclusive alone); pooled two-budget score 52.52%, interval 50.88–54.16. First cheap ablation: 420 versus 0 on current network/search, then a bounded option value based on safe reachable neutrals. Do not assume old result transfers. Enemy conversion opportunities still give leaders value after neutrals disappear. Adding conditional value atop existing proximity/access risks double counting.

3. **Recruitment: price race and retention rather than raw distance.** Current safe neutral access maxes at 60 versus material 1000; moving distance 2→1 earns 10. Replace independent distance discounts with bounded relative arrival/retention estimates, including whose turn, occupied routes and safe leader approach. Start with relative arrival using existing distances, not expensive full path planning per leaf. Raising RECRUIT_WEIGHT alone is lower priority: existing stronger-weight experiments failed to establish improvement. Case: 921574271 actions 29–51. Turn-dependent features REQUIRE changing evaluation-cache key: current key removes metadata including side and remaining turns. Do not add urgency/parity to evaluate_live while retaining a board-only cache.

4. **Search/pruning: protect quiet defensive/support moves.** Current depth-one futility pruning exempts leaders/tactics/hash/first move but can omit quiet cultist escapes and blockers before threat-aware quiescence sees their child. Cheapest candidate disables futility where material_threat(s); local test below shows this guard is insufficient in sampled divergences. Next bounded ablations: remove futility entirely, or exempt moves that break incoming fire / block a ray / support an exposed recruit. Compare identical-depth values first, then equal-time matches; removing pruning may lose depth. Unpruned comparison still uses selective quiescence, not full game-theoretic minimax.

5. **Quiescence/endgame: conversion-cycle handling.** Detect alternating conversion motifs and investigate exits before terminal horizon. Include remaining-turn parity, recruitment exits, shootings and neutral movement. Do not assign draw to repeated board arrangements: limit scoring and changing RNG invalidate that shortcut. A narrow extension when repeated conversions occur is safer to prototype than globally extending quiescence. Budget/cycle-path metadata must be reflected in cache semantics or cached bounds withheld where context differs. Cases 921572974 and 921574680; winning-loop control 921573637 must remain viable.

6. **Move ordering / NN: targeted ordering before larger network.** Add cheap safety/support ordering as a tie-break among quiet moves, retaining hash-move priority; potentially use root policy only when broad branching justifies cost. NN-off local probe below gives no reason to remove it or enlarge it. Training on retained-material/counterfactual choices could be valuable later, but root-only imitation cannot directly repair leaf value. Keep rules/simulator/RNG unchanged: latest audit matched 4,551 transitions.

## Local probes

`probe.py` freezes three isolated source variants and uses 20 selected positions across seven audited losses, three repetitions, alternating variant order, 48 ms budget. Fresh caches/history each position; reconstructed exact hidden RNG. This is not identical to arena's accumulated search history, hardware or scheduler. Positions are deliberately selected development data. No games played or uploaded.

- Baseline: mean depth 6.60; mean visits 409,110; max choose 48.006 ms.
- Futility guarded by `!material_threat(s)`: mean depth 6.65; visits 412,267; max 48.006 ms; actions differ from baseline in 3/60 comparisons. Timing noise and ordering effects prevent claiming improvement.
- NN disabled: mean depth 6.55; visits 413,496; max 48.011 ms; 0/60 action differences. More nodes did not produce more completed depth on this sample. No evidence for a strength improvement.

At fixed depth 4, futility on versus off changes values in 2/20 positions:

- 921574605 action 85: unpruned +1910 chooses MOVE unit 8 to cell 18; pruned +1933 chooses SHOOT unit 2 at leader 0.
- 921574605 action 89: unpruned +1037 versus pruned +1061; same action.

Threat-guarded futility leaves BOTH divergences unchanged. These demonstrate selectivity, not a proven arena blunder. They motivate broader defensive pruning ablations instead of shipping the simple guard.

`futility-depth4.json` includes the guarded comparison. `probe.py` regenerates baseline/pruned comparisons; guarded rows were added with the same harness and configure(1), depth=4, fresh setup per position. `probes.json` preserves all timed samples; `summary.json` preserves aggregate measurements.

## Practical limits and next gate

Current source 99,830 bytes: only 170 bytes below judge cap. Guard candidate is 99,853 bytes. Any substantive retained-material/cycle feature requires reclaiming source space (e.g. comments/formatting or better weight encoding) while preserving NN bytes and behavior; do not silently cross upload cap.

Prefer independent candidates: tunable leader weight, owned-unit safety, defensive-pruning exemption. Verify symmetry/cache correctness and tactical escape examples; measure evaluation cost. Then compare equal 48 ms, new maps, both seats, and independent Gold opponents. Cycle work follows with its own tests. Promote only after outcome evidence; none established by this investigation.
