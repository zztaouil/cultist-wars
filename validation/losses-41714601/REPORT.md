# Latest arena losses — submission 41714601

Snapshot: 2026-11-14. Latest 50 battle records at fetch, spanning completed games 16:07:05–16:18:42 UTC (17:07–17:18 Casablanca). 49 complete: 22 wins, 6 draws, 21 losses; one pending. This is a recent window, not the complete submission. Progress fetched shortly afterward: 97%, Gold #3/30, overall #22/401, score 24.99; BOSS 3 score 27.45. No causal version comparison is possible from these snapshots.

## Evidence integrity

Downloaded all 21 losses in the window plus ten latest non-loss controls (7 wins, 3 draws). Reconstructed 31 games using the unchanged Java model: every final score matches. All 4,551 transitions match submitted C simulator, including hidden RNG. Submitted source equals working bot.c; SHA-256 728cbc8412a487c31da12fa240f6c4bd5e8e3fb99332e02979d9ef017c3a64d0. No observed timeout or invalid-action failure. Across 2,262 diagnostic decisions, no RNG tracking failures after the first two own observations; one elapsed measurement exceeds 50 ms (51,541 us), without a forfeit. Timing margin is not guaranteed.

Controls are small, recent, and unmatched. Loss-only statistics describe mechanisms, not their prevalence among all games. No winning alternative has been proven by counterfactual search. No source changes, tests uploaded, submission, or IDE mutation.

## Main finding: durable material, not recruitment count alone

- 14/21 losses recruit fewer neutrals. Total loss-game recruits: 99–130; enemy kills: 50–70 (neutral kills excluded).
- Seven losses recruit equally or better: 921572071, 921572848, 921572974, 921573036, 921573787, 921574110, 921574605. Six lose exchanges; one loses enemy-conversion parity.
- 15/21 losses are tied or ahead in unit count at action 30; 11/21 at action 60; only 4/21 at action 100. Weakness extends well beyond the opening.
- Seven win controls: recruits 38–36, kills 20–15. Three draw controls: recruits 16–16, kills 5–5.
- Mean midgame completed depth: losses 5.50, wins 4.98, draws 5.33 (per-game means, own decisions 10–29). No evidence here that shallower average depth alone separates losses from wins; board complexity confounds this metric.
- Enemy leader remains >=9 HP at action 30 in 18/21 losses and all seven wins. Blanket enemy-leader shooting incentives are not supported.

## Concrete losses and investigation windows

All action numbers are zero-based global referee actions, not own turns.

### 921574271 vs BOSS 3: healthy leader stops recruiting

Final 3–6. Recruits 4–7; kills 2–2. Our last recruitment is action 27. Leader stays at cell 37=(11,2), 10 HP, at sampled actions 30–70. Boss recruits at 52, 64, and 148. At action 60 armies remain tied 5–5, two neutrals left. The entire final count gap equals recruitment gap. Investigate actions 29–51: supported leader routes and neutral access before gains become immediate.

### 921574383 vs BOSS 3: pressure without lasting economy

Final 2–5. Recruits 4–7; kills 3–3. Our 4-HP leader remains at cell 9=(9,0) at sampled actions 30–60. Boss leader is reduced to 1 HP by action 60 but survives and recruits at 72, 106, 108. Our unit 11 is recruited at 13, killed at 22; unit 10 recruited at 73, killed at 96. Dealing leader damage and obtaining temporary units do not guarantee final material.

### 921574605 vs FrancoisB: recruitment succeeds, army does not survive

Final 3–4 despite recruits 6–5. Kills 2–4. We lead 5–4 at action 60. Search still reports +1965 at action 81. Own units die at 78, 92, 102, 148; evaluation falls to +27 at 95, -994 at 107. Investigate actions 79–93 for coordinated fire, retreat squares, and support. A positive logged score is a heuristic forecast, not proof the position was winning.

### 921574110 vs BOSS 3: leader kill does not win combat

Final elimination 0–2; recruits equal 5–5, kills 4–6. We kill boss leader at action 35. Boss nevertheless kills our recruits and army; unit 6 recruited at 117 dies at 120. This directly contradicts treating enemy-leader elimination as a sufficient strategic target. Investigate later unsupported recruitment and trade decisions separately from opening recruitment.

### 921574564 vs Lv0Sys: leader loss removes future recruitment

Final 1–5; recruits 3–7, kills tied 3–3. Leader dies at action 25, shortly after our last recruit at 22. Opponent recruits at 29, 39, 65, 81, 137. Need conditional leader value based on remaining accessible neutrals and support, not one flat survival bonus. Investigate actions 16–24; early leader death may be consequence of an earlier positional mistake.

### 921572974 vs matsu7874: conversion parity trap

Final 6–8. Equal neutral recruits 6–6; neither side kills any unit. We recruit unit 4 at action 52; opponent converts it at 53. Repeated ownership exchange continues through 141: 44 own enemy conversions versus 45 opposing conversions. Final loss comes from ownership, not HP or firing. Need earlier cycle/remaining-turn analysis. A winning exit is not established.

### 921574680 vs Lv0Sys: conversion loop conceals recruitment exit

Final 4–5. Enemy conversion counts 50–50. Unit 13 is exchanged every action 40–139. Opponent exits by recruiting unit 3 at 140; we kill their leader at 141; opponent kills unit 13 at 144. Search reports forced loss at 143, too late to prevent the sequence. Investigate entry at 31–39 and exit opportunities before 140. A win control (921573637) also contains 58 conversions per side and wins, so globally forbidding loops would be wrong.

## Why current implementation still permits these patterns

Verified against submitted source, not historical README descriptions:

1. `recruit_control` at bot.c:264 only values currently neutral units. With weight 120, safe adjacent access is 60, one step farther is 40; a move improving distance 2→1 is only +10. A unit is worth 1,000 plus 18 per HP. This is a small positional nudge, not a retained-unit forecast. Increasing weight alone is unproven and previous holdout did not show a gain.
2. Risk uses only strongest current enemy shot. Even lethal exposure only halves the neutral-access bonus. It does not estimate future multi-action fire, safe retreat, allied support, or future blockers beyond the first step.
3. After conversion the new feature no longer evaluates that unit at all: `s->owned[2]` excludes it. General owned-unit evaluation has material/HP/proximity but no explicit exposure term. Tactical search can find immediate punishments; longer punishment chains can still cross its horizon. This is a plausible mechanism for retention failures, not a proof that every observed conversion was wrong.
4. Current source has a flat +420 leader bonus (bot.c:299), despite older README text describing zero. That fixed amount does not distinguish a safe leader with several accessible neutrals from a trapped leader or an exhausted board.
5. Terminal count is exact when searched; threat quiescence forces full replies only within its four-ply remaining horizon. There is no explicit long-cycle outcome solver. Ordinary alternating material values can defer recognizing bad conversion parity or a late recruitment exit. Do not treat the changing neutral/RNG state as an identical full state merely because combat units repeat.
6. NN scores root ordering; it does not supply leaf strategic value. Bigger network is not the direct remedy established by these logs.

## Recommended next experiments

1. **Retention and supported access**, evaluated together as a concept but ablated separately: bounded cheap owned-unit danger/support signal; leader routes toward neutrals that can actually be retained. Development positions: 921574605 actions 79–93 and 921574271 actions 29–51. Measure speed cost as well as score; existing access term already reduced local throughput in prior measurements.
2. **Cycle/endgame planning:** detect recurring conversion structures, compare remaining-turn ownership plus recruit/kill exits, preserve loops that secure wins/draws. Test both losing loops above and winning control 921573637.
3. **Conditional leader option value:** accessible remaining neutral opportunities and survivable routes; avoid a global enemy-leader damage bonus or simply increasing the flat leader bonus.

Before promoting any fix: fixed-position alternatives with longer search to identify plausible improvements; then new maps, both seats, equal safe budgets, several Gold opponents including BOSS 3. Track game score, recruit survival, final counts and forfeits. Keep these audited losses as development data, not holdout. No strength claim from this analysis alone.

## Reproduction

`python3 validation/losses-41714601/analyze.py`

`python3 validation/losses-41714601/summarize.py`

`python3 validation/losses-41714601/check.py`

`battles.json` freezes the requested recent window. `fetch.py` reads those IDs and caches logs; rerunning it refreshes progress only. `rows.json` contains events and phase counts; `reconstructed.json` contains full referee states/actions. Scripts reuse the prior audit's Java model and C harness.
