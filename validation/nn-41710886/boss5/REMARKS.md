# Five BOSS 3 tests — submission 41711135

Exact submitted NN C source, five preselected seeds 4171113501–4171113505.
Opponent confirmed BOSS 3 / agent 4611894 in all logs. All tests put us in seat 0.
Results: 2W / 1D / 2L, 50% score. No extra tests or arena submissions made.

| Game | Our units | Boss units | Result |
|---|---:|---:|---|
| 921367041 | 4 | 3 | Win |
| 921367102 | 0 | 3 | Loss |
| 921367143 | 2 | 1 | Win |
| 921367191 | 4 | 4 | Draw |
| 921367250 | 0 | 6 | Loss |

No observed timeouts, invalid actions, or RNG-disabled turns. Maximum internal
reported turn time 49.149 ms; not full judge timing margin. Java reconstruction
matches final scores in all five games.

Recruitment: both losses recruited 4 neutrals versus boss 6. Wins recruited 7–5
and 5–4; draw 6–6. Correlation in five maps, not a causal finding. Map accessibility
and first-seat advantage matter. No enemy conversions occurred in these games.

Unit preservation: in game 921367250 counts were 5–5 at turn 40, then 3–6 at turn
60; our units 6 and 5 died at turns 43 and 51. In game 921367102 counts were 4–4
at turn 40; deaths at 53/63 plus boss recruitment led to 2–4 by turn 80. Even the
4–3 win shrank from a 7–3 lead at turn 60. These are the useful decision windows.
Neutral kills occurred in a win and a loss; no evidence for a blanket prohibition.

Next: baseline ablation on these seeds to isolate policy impact (future API tests,
not performed here). Locally compare critical pre-loss decisions with deeper
search, preserve history/RNG, then distinguish horizon/pruning errors from leaf
valuation errors. Test one targeted safety/recruitment hypothesis at a time;
previous generic firing-support bonus failed and should not simply be repeated.
Use separate maps and both seats for confirmation. More policy epochs or larger
NN are not justified by this five-game sample; no guaranteed boss win strategy.
