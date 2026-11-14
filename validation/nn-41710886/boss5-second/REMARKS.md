# Five second-seat BOSS 3 tests

Replayed five historical battles with submission 41711135's exact source.
Confirmed BOSS 3 (4611894) in seat 0 and our bot in seat 1 in every returned log.
Selected first five qualifying second-seat boss games while scanning recent
submission history; no selection by original outcome. No arena or IDE changes.

| Test game | Us | Boss | Result |
|---|---:|---:|---|
| 921367596 | 5 | 5 | Draw |
| 921367656 | 4 | 2 | Win |
| 921367718 | 2 | 5 | Loss |
| 921367853 | 6 | 7 | Loss |
| 921367944 | 4 | 3 | Win |

2W/1D/2L, 50% score. Combined with first-seat batch: 4W/2D/4L.
Different maps across seats; not a paired seat experiment or NN ablation.
Java reconstruction final counts match all five logs. No timeout forfeits.

Both losses recruited fewer neutrals: us/boss 5/7 and 5/6. Wins recruited 6/5
and 6/3. Draw recovered 4/7 recruitment disadvantage through kills. Thus
recruitment matters, but material conversion through combat also matters.
Game 921367853 stayed boss 7/us 6 from turn 60 through turn 150: a stable
one-unit deficit that current search/evaluation did not overturn. Prioritize
recruitment-race decisions before the stable deficit, not simply deeper late
search or training longer. These are hypotheses from a small sample.

Timing: no forfeits, but maximum internal elapsed_us was 57511; other games
reached 54347 and 51625. Wall-clock scheduling/judge accounting can differ;
logs alone cannot isolate cause. Check timing margin before broadening NN use.

No further games started beyond the requested five.
