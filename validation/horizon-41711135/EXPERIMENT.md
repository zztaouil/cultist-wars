# Horizon and recruitment experiments

All candidates derive from submitted 41711135 (baseline.c), not the different
working bot.c. Existing production files, NN capacity, and 49-ms budget preserved.
Sources and SHA-256 hashes are in sources.json; standalone files remain <100 KB.

Hypotheses, isolated before measurement:

- contest: remove static material ownership credit for a living fighter adjacent
  to both opposing leaders. This approximates a contested conversion cycle by
  averaging ownership. Movement/shooting/proximity features remain unchanged.
  Terminal unit-count scoring remains exact; cache still board-only.
- extension: at the ordinary quiescence boundary, if an immediate material threat
  exists, allow one extra two-ply search. No recursive renewal. Remaining extension
  allowance participates in Q-cache identity. All threatened replies remain legal.
- combined: both independent switches. Set CONTEST_EVAL=0 and THREAT_EXTENSION=0
  to restore baseline behavior, checked against identical fixed-depth contexts.

Trace: exact reconstructed RNG; full-window child searches, futility disabled,
5-second cap per recomputed node. Stored lines show the chosen continuation at
successively decreasing horizons, including quiescence where it selects a move.
Stand-pat ends the line. These are heuristic principal continuations, not proofs.

Expanded fixtures: first recruitment deficit widening to at least two neutrals,
plus preceding own decision; directly convertible neutrals lost on the next
opponent move; neighboring turns 15/17/19/21/23 around the horizon example.
The direct-conversion screen finds zero instances. Actual game outcomes do not
label every sampled decision erroneous.

screen.py: persistent baseline 49-ms search along actual observations/actions;
tracker reconstructs hidden RNG. Copy the identical pre-decision quiet history
to each variant, clearing caches because evaluations/search semantics differ.
Compare depth four, 49 ms, and 500 ms. Probe budget excludes observation time;
actual bots include it. Prefix hardware/timing differs from arena.

Validation: existing exhaustive rays, transitions, minimax, quiescence, tracker,
and terminal tests against each source, with and without ASan/UBSan. Disabled
combined variant must match baseline action/value/nodes/Q nodes at fixed depth.

Maps: independently generate fresh referee seeds; canonical wall hash excludes
all maps in NN train/validation/test records, saved arena reconstruction, and
known map-list files. No mirrored duplicates. Eight development maps for each
variant, then sixteen separate holdout maps for the highest-scoring development
variant (ties: contest, extension, combined). Every map uses both seats at 49 ms;
two workers. Known-RNG local simulation does not reproduce judge scheduling or
tracker ambiguity. Small samples; intervals and failures retained.

Private BOSS 3 pilot: four identical cases per variant and baseline, two fresh
first-seat seeds plus two existing second-seat replays. Replay maps were used in
previous project tests, not fresh global holdouts. Rotate variant order by case.
New-game API always places submitted code first; replay API preserves second seat.
Explicit agent ID 4611894 and returned log verification ensure BOSS 3. No submit
or IDE-write endpoints. Scores, errors, RNG diagnostics, timings, logs retained.
