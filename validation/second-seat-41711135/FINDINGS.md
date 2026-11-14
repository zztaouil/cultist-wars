# First diagnostic pass

56 positions from all 14 verified second-seat losses in the saved 78% arena
snapshot. 28 early recruitment controls, 28 pre-material-loss positions. These
are event-adjacent positions, not 56 proven mistakes. Selection and reproducible
history/cache protocol are in README.md. Production source unchanged.

## Findings

- NN on/off changes 1/56 choices at 49 ms, 0/56 at 500 ms. At matched depth four,
  all 56 values agree; four chosen actions differ. This selected screen supplies
  no evidence for increasing model capacity. Timed probes are single samples.
- 500 ms reaches greater completed depth in all 56 positions, changes 4/56
  decisions relative to NN49. More depth does not imply uniformly better play.
- Disabling futility changes 2/56 choices; disabling threat Q changes 5/56.
  Equal-time comparisons also change depth. Neither ablation justifies removing
  the feature or establishes a pruning defect.
- Tracker checks cover 778 prefix observations (repeated prefixes included),
  with zero singleton RNG mismatches. All 112 depth-four probes completed.

## Matched-horizon follow-up

Six combat positions have differing actual/proposed choices and singleton RNG.
Score each candidate with identical fresh history, no futility, threat Q enabled,
at root horizons 4, 6, 8. All depth-four/six labels complete; two depth-eight
labels hit the two-second cap and are excluded. Results: labels.jsonl.

Most instructive: game 921365647, zero-based action 19.

| Action | Depth 4 | Depth 6 | Depth 8 |
| --- | ---: | ---: | ---: |
| Actual: `1 CONVERT 8` | -1195 | -1195 | -1195 |
| Local 49 ms: `11 MOVE 9 6` | -84 | -157 | -982 |
| Local 500 ms: `1 MOVE 8 4` | -1109 | -1106 | -484 |

The deeper choice gains 498 reference points over the local 49-ms choice at
matched depth eight, despite looking 949 points worse at depth six. This is a
concrete horizon-reversal fixture. Removing futility retains the 500-ms choice.
It supports investigating tactical continuation/ordering before enlarging NN,
not a claim that the counterfactual wins the game. Local49 differs from actual
arena action: local hardware/timing/history are not judge-identical.

Game 921364554, action 63: `12 MOVE 9 3` vs actual `13 MOVE 5 3` gains 845 points
at depth six, but only 12 at depth eight. Do not count the shallow large gap as
robust improvement. Several other differences are ties or small and unstable.

## Decision

No production candidate or submission from this screen. No demonstrated general
recruitment error: early opponent recruitment alone does not establish one.
Next focused experiment: trace principal variations for 921365647/action19 and
neighboring turns; determine which delayed material event causes the depth-six
to depth-eight reversal. Add positions where recruitment lead first widens or a
reachable neutral is lost, rather than treating first opponent conversion as an
error. Only then test a targeted extension/order/evaluation change on disjoint
maps in both seats. Existing diagnostic maps cannot serve as holdout.

Arena read-only refresh: 87%, Gold #3/30, rating 25.26; boss 27.20. Submission
41711135 still running, unchanged. No test-game, submit, or IDE mutation calls.
