# Second-seat diagnostic — submission 41711135

Frozen arena source; production bot and submission unchanged. Reproduce with
`python3 validation/second-seat-41711135/diagnose.py`, then `summarize.py`, then
`label.py`. Run sequentially on an otherwise idle machine.

Selection: all 14 verified second-seat losses in the saved 78% snapshot.
For each game, take the own decision before the first opponent recruitment and
first own material loss, plus the preceding own decision. Recruitment examples
are early controls, not established missed recruitment opportunities. First
material loss can be a sound exchange. This is an event screen, not error labels.

History control: reconstruct observations from Java states, omit hidden RNG,
run the real observation tracker, force actual arena actions. A 49-ms NN baseline
search advances persistent caches and quiet history at each prior own turn.
Each variant starts from an identical snapshot of that local prefix's TT,
Q cache, eval cache, and quiet history. Prefix timing is local, not an exact
reproduction of judge search history. Tracker time is excluded from probe budgets.

Compare NN on/off at 49 and 500 ms, plus depth four. Fixed-depth probes clear TT
and Q caches, retain identical quiet history, and explicitly initialize NN root
ordering. Ablations independently disable futility or threat Q; incompatible
search caches are cleared. Timed ablations confound speed and completed depth.
Timeout flags mean iterative search interrupted after its last complete depth;
fixed-depth interrupted results must be discarded.

`corpus.json` includes walls and complete state/action prefixes. `results.jsonl`
records choices, evaluation, depth, visits, and elapsed time. These are diagnostic
measurements, not outcome evidence or isolated first/second-seat treatment effects.

`label.py` verifies singleton inferred RNG against reconstructed truth throughout
selected prefixes. It evaluates actual, NN49, off49, and NN500 actions at the same
four-/six-/eight-ply root horizon (3/5/7 child plies), with fresh common history/caches,
futility disabled, and threat Q enabled. Each child has a two-second cap; null
scores are incomplete. These labels intentionally standardize history rather than
reproduce prefix ordering. Values share the engine's evaluation and bounded Q;
positive differences do not prove a game-winning correction.

No candidate selected yet. A prospective change needs separate maps, both seats,
and correctness/timing checks before any submission. No NN capacity change.
