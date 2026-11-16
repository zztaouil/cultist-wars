# Arena loss audit — 2026-11-14

Priority: upgrade recruitment-aware positional evaluation. Evidence supports this as the largest opportunity, not a proven strength gain. Also fix the confirmed timeout risk; it is a small, independent reliability task.

## Scope and validation

Read-only API snapshot: submission 41714430, Gold #3, overall #22/401, 81% ranked, score 24.86. Pagination exhausted at 81 records: 41 wins, 16 draws, 23 losses, one pending. Ranking still in progress. Player position 0 includes draws; outcomes were separated using both players' positions.

Downloaded every loss and the ten most recent completed non-loss controls (6 wins, 4 draws). Reconstructed all 33 with the unmodified Java referee model. Final counts match all 32 normal games. The remaining loss is a confirmed timeout, whose -1 score intentionally differs from board counts. Frozen submitted source matches working bot.c byte-for-byte, SHA-256 dda94ee98deb90b2c5fc1f6821af0b9587242aa2b5df012ca20e980b1cf6ac3b. All 4,748 reconstructed transitions match its C simulator, including RNG. No tracker failures after the first two own observations in 2,366 logged decisions.

Controls are recent and small, not matched maps or a random sample. Do not infer causal effect sizes from these comparisons. No tests submitted, no arena/IDE mutation, no production edits.

## Findings

- **17/22 normal losses have fewer neutral recruits.** Total recruits 103 vs 138; kills 58 vs 75. Six sampled wins: recruits 32 vs 29, kills 20 vs 13. Four draws: recruits 21 vs 21, kills 8 vs 8.
- **Five losses have equal/better recruitment but lose exchanges.** 921558512, 921558672, 921559790, 921560273, 921560889. Recruitment counts alone cannot solve these.
- **Similar search depth in losses and wins:** mean per-game midgame depth 5.44 vs 5.48, using own decisions 10–29. This does not prove deeper search is useless; it does not identify throughput as the leading separator.
- **Enemy leader HP alone is a poor discriminator here.** HP >=9 at action 30 in 18/22 losses, but also all six wins and four draws. Earlier boss-only pressure hypothesis should not be generalized into a blanket shooting bonus.
- **Confirmed timeout:** 921559739, frame 53 / action 52: `$0 timeout!`, score -1–0. Last stderr reports 49,027 us; process output/flush/judge scheduling is outside that measurement. Across sampled logs, 7 decisions exceed 50,000 us, max 62,049 us. This does not identify exact timeout cause, but 49 ms budget lacks reliable margin.

## Concrete failure points

All action numbers below are zero-based referee turns, not own-move numbers.

### 921562886 vs FrancoisB — recruitment stops despite healthy leader

Our second-seat army leads 5–4 at action 30; ends eliminated, 0–4. Last own recruitment at action 23. Opponent recruits at 32, 54, 70, 78; total recruits 4–8 despite equal kills, 5–5. Own leader stays at cell 35 = (9,2) through sampled actions 23–61, 10 HP. At action 60 armies are tied 3–3, two neutrals remain; opponent gets both. Own logged advantage falls from +1064 at action 23 to -1237 at 61.

Upgrade target: evaluate future neutral access and support for advancing the leader before recruitment becomes a visible tactical gain. Decision window to investigate: actions 25–53, preceding the last three opposing recruits. No winning alternative proven.

### 921562545 vs Peterpanda — recruitment economy collapses

First seat, final 3–7. Recruits 2–7. Our bot kills three neutrals at actions 14, 38, 52; whether those denials were locally justified requires counterfactual search. Own final recruit at 28; opponent recruits at 33, 51, 61, 71. Own leader oscillates along top-left cells while opponent collects remaining neutrals. At action 28 value -205 becomes -2178 at action 30, exposing a sharp horizon change even with completed depth 6–7.

Upgrade target: contested-neutral arrival estimates, retention probability, and occupancy/support-aware access. Do not simply reward every neutral or forbid neutral shots.

### 921558512 vs ElectricTyphoon — recruits fail to survive

First seat, final 5–6 despite recruits 7–5. Opponent kills three units; we kill none. Unit 6 recruited at 22, killed at 25. Unit 10 recruited at 46, killed at 59. Unit 4 recruited at 38, killed at 77. Value +1061 at 46 remains +1056 at 50, then drops near zero after unit 10 dies. These trajectories motivate a second component: longer-term fire exposure and safe conversion valuation. They do not prove each recruit was avoidable or wrong.

### 921562279 vs Lv0Sys — repeated conversions while behind

Second seat, final 1–4. Unit 12 converted back and forth at every action 48–134 (43 own conversions, 44 opposing conversions). Logged value stays around -1100 over much of the loop. Search only resolves the terminal count once the horizon reaches the limit. Loop eventually breaks, but no recovery. Another loss, 921562783, has seven conversions each; boss control 921561935 has 53 each and draws.

Upgrade target: recognize repeated board cycles plus remaining-turn parity, especially when losing. Do not ban repetition globally: conversion loops can secure draws. Need counterfactual evidence that an earlier exit improves outcome.

## Upgrade order

1. **Recruitment-aware evaluation (`bot.c:246`, `evaluate_live`).** Current leaf score prices units, HP, flat leader bonus, and obstacle-only proximity. It does not explicitly price who reaches a neutral first, whether that route is occupied/unsafe, or whether supporting cultists can keep the recruit. Add a cheap contested-access/expected-retained-recruit feature. Keep it bounded and validate speed; naive symmetric race bonuses previously failed. Expected largest strategic benefit; unproven until tested.
2. **Safe-conversion/fire-exposure evaluation.** Cover the five losses without recruitment deficits. Use actual ray/damage support, not Manhattan distance alone; investigate selective tactical extension only where unsafe recruitment causes a horizon reversal. Avoid global expensive leaf scans.
3. **Cycle/endgame planning.** Detect repeated board configurations, account for turn parity and terminal counts, compare exits when behind. Lower observed frequency than recruitment failures.
4. **Time-budget safety — immediate parallel priority.** Test 47–48 ms and reserve output overhead; measure judge failures and equal-time strength before choosing. One confirmed lost game can be recovered only if it would otherwise draw/win; no automatic win claim.
5. **Larger root-ordering NN / generic extra depth.** Lower priority. NN currently orders root moves; it does not repair missing long-term leaf values. Current logs do not establish NN benefit or harm. More capacity without a changed target is not the first experiment.

## Next experiment

Freeze this submission. Label early contested-neutral decisions from these losses as development data, not evaluation data. Compare one bounded recruitment/retention term against baseline at the same safe time budget. Use new maps, both seats, several Gold opponents including BOSS 3, alternated runs. Primary metric: game score; secondary: neutral recruits retained ten actions later, final material, timeout rate. Report uncertainty. Keep separate tactical and cycle changes separate until their individual effect is measured. No arena submission justified by this audit alone.

## Artifacts / reproduce

`progress.json`, `submissions.json`, `battles.json`, `page-*.json`, `all-battles.json`, `game-*.txt`: raw API records. `reconstructed.json`: Java states/actions. `rows.json`: per-game events and metrics.

Run `python3 validation/arena-loss-audit-20261114/analyze.py`, then `summarize.py`, then `check.py`. Java path is local Homebrew OpenJDK; C checker builds an isolated shared library. `fetch.py` requires the existing token and reads submission 41714430; repeated fetches may include later battles.
