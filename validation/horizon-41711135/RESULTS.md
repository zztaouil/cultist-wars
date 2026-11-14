# Implemented experiments and BOSS 3 results

**No candidate demonstrated enough improvement to replace the current bot.**
All three standalone variants were implemented, validated, and tested against
BOSS 3. Arena submission, IDE, working bot.c, NN capacity, and 49-ms budget remain
unchanged. See EXPERIMENT.md for the predeclared methodology and source switches.

## Implemented remarks

1. Traced the delayed-exchange example at actions 17/19/21/23, root depths six
   and eight, including quiescence continuations and material changes.
2. Replaced first-opponent-recruit controls with 20 positions around the first
   recruitment deficit widening to at least two, plus five neighboring combat
   positions. Found zero directly convertible neutrals lost on the immediately
   following opponent turn in the 14-loss screen; this does not exclude longer
   sequencing errors.
3. Implemented contested-unit evaluation, bounded threat extension, and their
   combination as separate candidates derived from the frozen actual submission.
4. Compared candidates with matched quiet history and cleared incompatible
   caches, fixed depth four, 49 ms, and 500 ms. Replayed the real observation
   tracker along actual played actions. Checked disabled-feature parity.
5. Ran separate-map matches in both seats, then untouched holdout for the best
   development candidate. Tested every variant and baseline on identical BOSS 3
   cases in both seats.

## What caused the reversal?

The baseline principal continuations include repeated conversion of unit 8
between the leaders. Static ownership at a finite leaf heavily favors whichever
side converted last. In the leader-retreat continuation, player 1 instead moves
from (8,5) toward (8,2), recruits unit 13, then trades fighter 11 for leader 0
through a delayed shooting sequence. The depth-six continuation differs from
that depth-eight sequence; there is no single missed immediate shot.

These are heuristic search continuations, not the actual opponent's future play.
At the original action-19 fixture, the baseline at 49 ms chooses `11 MOVE 9 6`;
at 500 ms it chooses `1 MOVE 8 4`. Threat extension chooses that latter move
already at 49 ms. But at 500 ms the extension returns to conversion, so it does
not establish a stable resolution of the horizon problem. The contest variant
also changes the reference evaluation itself; its values cannot be compared
numerically as improvements over baseline values.

## Local outcome tests

Eight development maps, both seats, 49 ms per bot. Candidate scores versus frozen
baseline; these small development results selected contest for holdout.

| Candidate | Development W/D/L | Score | BOSS 3 W/D/L |
| --- | --- | ---: | --- |
| Baseline control | — | — | 2/0/2 |
| Contested-unit evaluation | 9/0/7 | 56.25% | 1/0/3 |
| Threat extension | 5/3/8 | 40.62% | 2/0/2 |
| Combined | 4/3/9 | 34.38% | 1/0/3 |

Contest holdout: **14W/5D/13L, 51.56%** over 32 games / 16 untouched map pairs.
Approximate paired 95% interval: **44.53–58.59%**. Seat-zero score 62.50%;
seat-one score 40.62%. No demonstrated gain. The interval uses map-pair averages;
seat differences still describe this matchup, not an independently isolated
causal seat effect.

Fresh map generation excluded canonical wall layouts in NN records, known
map-list artifacts, and saved arena diagnostics, including reflections. The
32 generated maps are mutually distinct; eight development, sixteen holdout,
eight reserve. BOSS first-seat tests use two reserve maps. The local engine uses
known RNG and does not impose judge timeout forfeits; judge tests complement it.

## BOSS 3 details

16 completed private tests: four variants × four identical cases. Two fresh
first-seat seeds (41712024, 41712025), two historical second-seat replays
(921359954, 921338908). Variant order rotated. Verified opponent is BOSS 3,
agent 4611894, in every returned log. Historical replays were used in earlier
project testing; they are not globally unseen holdouts.

Every variant lost both second-seat cases. Baseline and extension won both
first-seat cases; contest and combined each lost seed 41712024 and won 41712025.
Thus extension matches baseline outcomes in this small boss pilot, while both
contest variants lose an additional first-seat case. Four cases cannot resolve
small strength changes, but provide no promotion evidence here.

No crashes, invalid-action errors, timeout forfeits, or zero-candidate RNG
observations in the 16 returned test logs. Maximum reported internal elapsed:
baseline 54.171 ms; contest 49.065 ms; extension and combined 49.067 ms.
Internal elapsed time is not judge CPU accounting or proof of timing safety.

## Cost and correctness

- All four sources pass the existing exhaustive engine tests, optimized and
  ASan/UBSan builds: eight successful runs. Standalone C11 builds pass.
- Combined variant with both switches disabled exactly matches baseline action,
  value, main/Q node counts, and completion status on all 25 fixed-depth fixtures.
- All 100 candidate/baseline depth-four probes complete.
- Extension uses 994,899 versus baseline 426,878 nodes at depth four: **2.33×**.
  Mean completed depth at 49 ms falls from 7.24 to 6.88. Same nominal depth is
  not the same total horizon because extension searches additional Q plies.
- Contest changes 1/25 timed choices and 0/20 recruitment choices; extension and
  combined change 5/25 and 3/20 respectively. Choice changes alone are not gains.
- No board-transition changes. Terminal unit-count scoring remains exact.
  Extension is bounded to one additional two-ply block; allowance enters the
  Q-cache identity. Contest evaluation uses only board features, preserving the
  board-only evaluation-cache key.

## Decision and artifacts

Do not promote these variants. Retain the failure evidence: an attractive
single-fixture correction did not generalize, and broad extension cost offsets
its tactical reach. Contested ownership averaging is also insufficient by itself.
No evidence from this work calls for enlarging the NN.

Sources: baseline.c, contest.c, extension.c, combined.c; hashes: sources.json.
Raw evidence: traces.json, fixtures.json, screen.json, ablation-parity.json,
checks.json, matches/*.jsonl, boss/*.json, summary.json. Reproduction scripts:
variants.py, fixtures.py, trace.py, check.py, screen.py, matches.py, boss.py,
summarize.py. boss.py only uses private test-game endpoints; no submission or
IDE mutation endpoint exists in that runner.

Original submission 41711135 completed ranking: **Gold #3/30, rating 25.15**;
BOSS 3 rating **27.42**, not promoted. Saved in progress-latest.json.
