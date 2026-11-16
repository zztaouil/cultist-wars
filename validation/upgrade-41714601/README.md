# Component upgrade — 2026-11-14

Submitted as **41714747**, explicitly requested. Arena ranking pending.

Baseline arena submission 41714601. Implements five bounded changes; no claim all diagnosed weaknesses are solved.

- Owned-unit safety: strongest incoming shot from all legal aim targets, including indirect victims; penalty 12 × damage plus 90 when two such hits can kill. Applied symmetrically to leaders/cultists. This is a positional heuristic; it does not prove two shots are available or model a full support/escape plan.
- Leader value: flat +420 changed to configurable LEADER_VALUE=0. Existing proximity and recruitment values remain.
- Recruitment race: configurable 60/(distance+2) bonus for strictly closer leader, using existing occupied-first-step distance. Ties receive no extra bonus. Existing exposure-discounted neutral access remains. Does not predict all future blockers or neutral movement.
- Defensive pruning: suppress depth-one futility pruning when own units face conversion/lethal threats or any current incoming fire. Suppress it in final six actions. This protects quiet replies in exposed positions, but is not a proof all useful support moves survive pruning.
- Conversion extension: quiescence gets six instead of four plies when both leaders can convert the same owned cultist. This searches exits and terminal parity earlier. It is not repetition detection or an exact long-cycle solver; two selected conversion losses remain losses.

All features except budget have compile-time ablation switches (manifest). NN bytes unchanged. Rules and RNG simulation unchanged. Evaluation remains independent of side-to-move and remaining turns; board-only evaluation cache remains valid. Q bounds already include exact tactical depth, so four-/six-ply values cannot alias. No path-dependent cycle scores introduced.

Source reduced to 95,665 bytes by stripping comments/indentation while preserving string literals. `build_candidate.py` regenerates candidate from frozen baseline with readable inserted functions. `candidate.c` equals production bot.c; SHA-256 in manifest. Source remains under 100,000-byte judge limit.

## Checks

`make test`: existing rays/transitions/minimax/tactical/RNG/tracker/search/cache tests, recruitment tests, new component tests, protocol all pass. New component fixtures check exact incoming damage, opposing ownership symmetry, cached side sign, wall shielding, contested conversion and missing leaders. `make sanitize`: all four test executables pass ASan/UBSan. Existing long-string and unused-function warnings remain. Protocol maximum 50.2 ms locally; startup included and no judge deadline enforced by that test.

32-game fresh-seed, seat-paired local development at equal 48 ms: 14W/8D/10L, 56.25%, approximate paired 95% interval 44.85–67.65. Separate 64-game holdout uses subsequent independent seeds and the unchanged candidate; 25W/19D/20L, 53.91%, approximate paired 95% interval 43.80–64.01; results saved in results.json. These are same-family C self-play, with four concurrent workers and exact RNG, not independent arena opponents. Seed independence does not guarantee wall-layout independence or absence from historical training. No component-wise strength attribution is possible from this bundled change.

Four private replays use original loss seats/opponents/seeds, candidate and rerun baseline. Both candidate and rerun baseline score 0W/1D/3L. Results saved under judge-*.json; summary in results.json. Candidate has no timeout/errors; maximum logged elapsed 48,196 us. Both conversion-loop cases remain losses. Judge games vary with timing: compare rerun baseline, not original loss outcomes alone. No IDE overwrite.

## Reproduction

- `python3 validation/upgrade-41714601/build_candidate.py`
- `make test` and `make sanitize`
- `python3 validation/upgrade-41714601/holdout.py` (resumes saved match files; fresh tag needed after source changes)
- `python3 validation/upgrade-41714601/private_tests.py candidate` / `baseline` (uploads source, caches completed runs)

Arena submission explicitly requested by user. Submission receipt and progress snapshot stored here when accepted. Tests support deployability; small score samples do not establish arena-strength gain.
