Arena update: user requested submission; accepted as **41714601**. Ranking in progress. Earlier no-submission notes below are historical.

# Recruitment evaluation — 2026-11-14

Implements the first arena-audit priority: prospective neutral access, discounted for exposed recruits. Baseline is submission 41714430. The experiment also prepares a 48 ms default budget to reserve more judge/output margin after a confirmed 49 ms timeout. This is mitigation, not a guarantee against judge timeouts.

## Evaluation

`recruit_distance` estimates actions before conversion: zero when adjacent, otherwise a legal unoccupied first step followed by obstacle-only distance to the neutral. It handles dead/missing leaders and unreachable regions. Future unit occupancy and neutral movement remain approximations; search still simulates both normally.

For each neutral and side, access value is `120 * (2*hp - risk) / (2*hp*(distance+2))`. Risk is the strongest current legal direct shot from an opposing cultist, capped at neutral HP. Actual referee rays and blockers determine that shot. One opponent action can fire one unit, so damage is not summed. Side-zero access minus side-one access augments the existing leaf score. Equal access cancels. No term remains after all neutrals disappear. Existing material, leader, HP, search, and network weights remain unchanged.

This is a bounded positional heuristic, not a calibrated conversion probability. It does not yet add general exposed-cultist penalties, cycle detection, or larger networks. Those are separate lower-priority changes. Simply increasing generic race weights failed earlier experiments; this implementation adds occupancy and retention information and uses a modest weight.

`RECRUIT_WEIGHT=0` compiles out the term for ablation. Evaluation remains board-only, preserving the cache's independence from side, remaining turns, and neutral RNG. Source string reflow preserves NN bytes exactly while keeping the standalone source below 100,000 bytes.

## Validation protocol

- Development: 32 fresh canonical layouts, both seats, baseline vs weight 120, 20 ms each. Weight 120 screened first; weight 360 tested after the first holdout was flat.
- Holdout: 64 additional layouts, both seats, 48 ms each. Layouts distinct from development and existing `*maps*.json` artifacts under reflection. Not claimed absent from all historic training data.
- Four local worker processes. Host load and timing variation remain limitations. Self-play against baseline does not establish transfer to the Gold arena.
- Fixed-depth ablation: 24 real positions, feature disabled, exact moves/scores/node counts match baseline.
- Tests: access distance, occupied first steps, direct fire exposure, owner symmetry, cache sign, dead leaders, empty neutral sets, disconnected regions; existing rules, minimax, tactical, RNG and tracker tests; ASan/UBSan.
- Private judge tests were blocked by automatic approval review because they upload baseline and candidate source. No private test was started, no submission or IDE mutation performed.

Development result: 32W / 10D / 22L, 57.81%; paired approximate 95% interval 50.05–65.57%. Separate 128-game holdout at 48 ms: 46W / 36D / 46L, 50.0%; paired approximate
95% interval 43.64–56.36%. Stronger weight 360 on development maps: 26W / 15D /
23L, 52.34% (42.66–62.02%). No measured strength gain is established. The 120
setting is retained as the implemented candidate because the stronger setting
has no clear benefit. Production now contains it, with the baseline and compile
switch available for rollback; arena submission remains untouched.

32 real-position local probes at equal 48 ms: mean completed depth 6.28 baseline
versus 5.94 candidate; mean visits 429,643 versus 319,300. Uncached leaf evaluation
28.46 ns versus 104.03 ns. Four of 32 selected moves changed. This cost matters:
the evaluation improves information but reduces search throughput. These probes
use reconstructed hidden RNG and local fresh history, not judge-identical input.
Maximum local choose time was 48.006 ms; this does not measure judge scheduling
or prove timeout elimination.

An exact precomputed-value-table variant (`fast.c`) was also examined. It did not
improve sampled throughput (99.86 ns direct vs 106.02 ns table); the simpler
arithmetic version is retained. Any extra-map results for that variant are
exploratory and are not pooled into the primary holdout result.

## Reproduction

`python3 validation/recruitment-20261114/run.py recruit120 validation/recruitment-20261114/candidate.c`

`python3 validation/recruitment-20261114/run.py recruit_holdout48 validation/recruitment-20261114/packed.c 48 holdout.json`

`python3 validation/recruitment-20261114/verify.py`

`python3 validation/recruitment-20261114/measure.py`

Match files are append-only/resumable. Do not reuse a tag after changing source. `baseline.c` is the frozen submitted source; `candidate.c`, `packed.c`, and `selected.c` preserve development and standalone variants. Packing only joins adjacent NN string literal chunks. `selected.c` shortens the new comments and sets the default to 48 ms; the holdout explicitly overrides both arms to 48 ms.

Additional table-variant sample: 32 new maps, both seats, 48 ms, 25W/15D/24L
(50.78%; paired approximate 95% interval 42.12–59.44%). This also provides no
clear improvement. The table variant remains isolated.

Final production `make test` and `make sanitize` passed. Protocol test checks
legality and reports latency; it does not enforce the judge deadline. Its maximum
230.5 ms includes first-turn initialization and was measured while match workers
were running. The isolated `choose` measurements above are the relevant local
search-time observations; neither replaces judge timing validation.
