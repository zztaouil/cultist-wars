# Expanded top-ten imitation dataset — 2026-11-14

Target: 5,000,000 unique training inputs including approved unit-ID relabeling,
plus separate original-only
validation/test positions. User approved both original and refreshed top-ten
cohorts: their union contains 11 players. Frozen snapshots and the exact teacher
allowlist are in `leaderboard-original.json`, `leaderboard-current.json`, and
`leaderboard.json`. Historical versions of these players are eligible.

The existing `../imitation-1m-20261114/` dataset is preserved.

## Collection

`collect.py` uses paced CodinGame HTTP API requests. It first reads battle lists
for approved teacher versions and the full arena roster. Opponents' lists are
used to discover additional teacher replays and historical teacher agent IDs;
opponents outside the allowlist never supply training labels. Cached requests
make interrupted collection resumable. HTTP 429 honors `Retry-After`.

The existing replay corpus is merged by immutable game ID. Original replay
actions, seeds, scores, teacher provenance, and source battle lists are retained.
No locally generated games, guessed expert actions, or duplicate input rows
contribute toward the target. After discovery demonstrated that replay history
with four reflections could not reach 5M, the user explicitly approved unit-ID
relabeling augmentation to reach the target. These variants repeat tactical
boards; they are not additional played positions.

## Preparation and audit

`prepare.py` independently reconstructs games using the existing Java referee
model, checks final scores, and verifies every transition with the frozen C
engine. Timeout/empty games are excluded. Only approved teachers' turns count.

Same augmentation and split rules as the original dataset: identity plus
horizontal/vertical/both reflections, with actual shot-victim verification;
canonical wall layouts determine 80/10/10 map groups; heldouts contain originals
only. Observable states, including unit IDs, are deduplicated across the entire expanded corpus.
Dead-unit fields and hidden RNG do not inflate uniqueness. Map splits are stable
across old and expanded datasets. These heldouts have appeared in prior research.

Preparation completes all identity-ID boards and reflections first, preserving
the original dataset. Additional passes apply deterministic bijections to unit
IDs 2–13, keeping leader IDs 0 and 1 fixed. Unit slots, action actor/target IDs,
and the hidden neutral mask are relabeled consistently. Every transformed label
must match the freshly generated legal actions, and actual shot victims must
map consistently. Features are recomputed from the transformed state.
This does not assert that future neutral RNG trajectories or optimal choices
are invariant under relabeling. Teacher action provenance remains the real replay.

`cache/records.jsonl` preserves per-position game, turn, player, reflection,
ID-permutation index, synthetic flag, map-group, observable-state digest, and
tactical digest ignoring unit IDs. `cache/meta.json` stores every deterministic
old-to-new ID mapping. `cache/report.json` explicitly records
whether 5M training positions were reached. `audit.py` checks teacher membership,
labels, file sizes, ID-aware uniqueness, tactical-board counts, map separation,
original-only heldouts, synthetic-example counts, and how many original dataset
states are present. It needs only Python's standard library.

Run from repository root:

```sh
python3 -u validation/imitation-5m-20261114/collect.py
python3 -u validation/imitation-5m-20261114/prepare.py
python3 validation/imitation-5m-20261114/audit.py --require-target --require-original
```

Preparation requires a C compiler and JDK. Cache directories must be new.
Features retain the existing float32 format and 128 features per legal action;
5M positions are expected to require roughly 45–50 GB, depending on action counts.
Training is outside this dataset expansion request.

## Completed result

Collected 4,463 replay logs after checking 507 bot histories and discovering 117
historical teacher versions. This merges 3,547 existing approved-cohort logs with
916 newly downloaded logs; one additional requested game was unavailable.
Excluded 105 timeout/empty games and three reconstruction/transition failures.
All 4,355 retained games passed final-score checks and 600,932 C/Java transition
checks.

- Training: **5,000,000** unique observable inputs including unit IDs.
  - **412,135** original played positions.
  - **1,215,989** reflected positions with original IDs.
  - **3,371,876** ID-relabeled variants, some also reflected.
- Validation: **50,601** original played positions.
- Test: **50,291** original played positions.
- Total: **5,100,892** unique ID-aware inputs, from **513,027** source game/turns.
- Distinct tactical boards ignoring IDs: **1,729,016** overall;
  **1,628,124** in training. ID variants do not add tactical boards.
- Canonical map groups: **4,343**, with no train/validation/test overlap.
- All **1,066,179** original-dataset states retained; original files preserved.
- Feature cache: **47,137,124,864 bytes**, float32, compatible with the existing
  memory-mapped policy trainer's `--cache` option.

`cache/audit.json` reports `target_reached: true`, zero duplicate ID-aware inputs,
zero original-dataset missing states, no map leakage, and original-only heldouts.
`manifest.json` freezes code, source, cohort, metadata, and record checksums.
