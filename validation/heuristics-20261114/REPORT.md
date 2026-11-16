# Arena loss heuristics — 2026-11-14

## Evidence and attribution

Current submission 41714969, C++, Gold #3 / overall #22, rating 25.48,
91% ranked at snapshot. Recent page: 8 wins, 4 draws, 8 losses. Downloaded
six latest losses; this is a selected diagnostic sample, not a strength estimate.
Frozen baseline.cpp matches submissions/41714969.cpp exactly. The /code endpoint
returned older C IDE code; it was not used as the arena baseline.

Independent Java model reconstructs all five normal final scores exactly.
The sixth game, 921597897 vs N3l, explicitly forfeits on timeout (frame 30).
No tracker failures after initial observations. Maximum logged decision 50,579 us.

- 921597656 vs BOSS 3: recruits 4–7, kills 4–4, final 1–4.
  Leader loses 5 HP at actions 21/23 while six neutrals remain.
- 921597759 vs BOSS 3: recruits 5–6, no kills, final 6–7.
  Healthy leaders: leader safety alone cannot solve this access/race loss.
- 921598429 vs BOSS 3: recruits 5–7, kills 3–2, final 4–5.
  Leader 10→2 HP at actions 28/30/32 with four neutrals left.
- 921598028 vs YALOKGAR: recruits 6–6, kills 1–5, final 1–7.
  Leader 10→2 HP while the final neutral remains; one hostile conversion.
- 921598264 vs YALOKGAR: recruits 5–5, kills 2–4, final 2–4.
  Leader 10→1 HP by action 35 with four neutrals left; dies action 81,
  two neutrals still available.

## Systems and implemented heuristics

1. safety_value treated leader damage exactly like cultist damage. Leaders
   preserve access to future conversions, a value shallow search may miss.
   Add 18 × strongest legal incoming damage × min(remaining neutrals, 3)
   to leader exposure penalty. Actual bullet rays, blockers and indirect hits
   come from existing incoming_damage. No extra ray scan. Vanishes when no
   neutrals remain; bounded at 324 points per leader. LEADER_EXPOSURE=0 ablates.
2. recruit_control discounted gunfire but counted adjacent enemy-leader access
   as secure recruitment potential. Halve prospective access when the opposing
   leader is already adjacent to that neutral. This reflects immediate recapture
   risk, not a proof of forced loss. Both sides discount symmetrically when both
   leaders are adjacent. RECAPTURE_DISCOUNT=0 ablates. Historical conversion-loop
   losses also motivate this; the fresh sample does not establish its frequency.

Both features stay board-only, preserving evaluation-cache validity. No action
is forbidden; minimax can still accept exposed recruitment or contested captures.
Network, simulator, search and 49 ms budget unchanged. Existing user work and C
baseline preserved. These heuristics do not solve all route planning, unit trades,
conversion-loop parity, or timeout risk. The 49 ms default still leaves little
judge/output margin; prior 46 ms variant exists in the archived submission.

## Verification and limits

make and make test pass: engine, tactical search, recruitment, components,
C++ regression fixtures, ASan/UBSan, protocol. New fixtures check leader exposure
with/without neutrals, ownership symmetry, contested equal access, and numerical
recapture discount. Final added fixture also run under ASan/UBSan separately.
Warm protocol maximum 51.3 ms includes transport overhead; not judge validation.

Equal-time local comparison uses eight shuffled real maps, both seats, 49 ms,
against frozen baseline. Same-family simulator knows neutral RNG. First games
overlap test compilation; remaining games run without other local test workers.
Small exploratory sample, no untouched holdout or arena-strength claim. No judge
uploads, arena submission, or IDE overwrite performed. Raw matches retained.

Reproduce with a fresh tag (existing matches are append-only):

    make test
    python3 validation/heuristics-20261114/analyze.py
    python3 validation/heuristics-20261114/summarize.py
    python3 tests/arena_match.py --a validation/heuristics-20261114/baseline.cpp --b bot.cpp --tag fresh --out validation/heuristics-20261114 --maps-used 8 --shuffle-seed 993821 --budget-ms 49

## Completed comparison

16 games: candidate 6W / 3D / 7L, score 46.88%; approximate map-paired
95% interval 29.71–64.04%. No demonstrated gain; point estimate slightly below
baseline. Heuristics remain an experimental local implementation of the requested
coverage, not a recommended arena deployment. Frozen baseline and compile-time
ablations retained. Broader paired tests and matched judge replays are needed
before claiming improvement. Strict standalone C++17 build passes; 96,462 bytes.

## User-authorized submission

Submitted unchanged validated C++ source as 41715067. Receipt and initial progress archived. Ranking pending; no strength gain claimed.
