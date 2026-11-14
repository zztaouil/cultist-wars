# Submission 41711135 at 78% ranking

API snapshot: rating 25.14, global 22/401, Gold 3/30; BOSS 3 rating 27.20,
not promotion eligible. 73 finished battles: 41W/14D/18L, score 65.75%.
Seat 0: 26W/7D/4L (79.73%); seat 1: 15W/7D/14L (51.39%). Two second-seat
wins are opponent turn-zero timeouts; excluding those, seat-1 score 48.53%.
Opponent/map schedules differ, so this is descriptive, not isolated seat effect.

Arena boss record: 4W/1D/2L. First seat 3W; second seat 1W/1D/2L.
Beating boss in several battles does not imply ranking above boss globally.

Reviewed all 73 logs; Java reconstruction final counts match all 71 normally
played games. No own timeout forfeits or invalid-action errors found; RNG tracker
never reports zero candidates. Maximum reported own elapsed time 53.403 ms.
Internal wall-clock timing is not a full judge CPU-time measurement.

Loss patterns: 10/18 losses recruited fewer neutrals; remaining 8 recruited
at least as many and all lost more units to opponent kills than they killed.
Recruitment deficits and combat survival both matter. End-of-game recruitment
counts are correlational: leader death and losing positions can themselves stop
recruitment. Do not treat them as proof of an opening defect.

Examples: 921368268 vs boss recruited 6–6, killed 3–5, lost 2–4.
921374717 vs boss recruited 4–6, killed 4–5, lost 0–3.
921370761 vs N3l recruited 6–6, led 7–6 at turn 80, fell to 3–5 by 120.
921369019 vs matsu7874 recruited 4–7 despite favorable 2–1 kill count, lost 4–6.

Recommended experiment order:
1. Preserve current submission through completed ranking; no model expansion yet.
2. Build second-seat diagnostic set of pre-loss positions from both failure types.
   Compare policy on/off with persistent state/history, matched fixed depth,
   49-ms budget, and deeper offline search. Ablate futility/threat quiescence
   separately to locate horizon/pruning effects before changing evaluation.
3. If deeper search fixes choices, prioritize targeted tactical extensions/order;
   if deeper search keeps bad choices, revise safety/recruitment evaluation or
   train a small value model from outcomes/search labels. Avoid generic firing
   support bonus already rejected in earlier project work.
4. Confirm one candidate on separate paired maps plus boss replays. Root-policy
   same-family controlled pilot remains 50%; arena schedule cannot isolate NN gain.

Sources: progress-78.json, battles-78.json, game-*.txt, study78.json,
recon78.txt and study78.py. Read-only API use; no new test games or submissions.
