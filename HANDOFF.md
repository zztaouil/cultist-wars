# Cultist Wars — Claude Code handoff

Snapshot: 2026-11-13, updated after the tuning pass (§9). Reconstructed from workspace, experiment reports, saved progress, prior project chats. No live arena refresh during handoff. Relative paths below resolve from repository root.

## 1. Goal / immediate state

Build competitive standalone C11 CodinGame bot; reach Legend through better equal-time decisions. Current engine already optimized; focus decision quality, evaluation, tactical efficiency. Do not repeat LMR or chase MSz's reported nominal depth without new evidence.

Workspace: `/Users/hexagone/Documents/ChatGPT/cultist-wars`.
Remote: `https://github.com/zztaouil/cultist-wars.git`; branch `main`.
Game: `https://www.codingame.com/multiplayer/bot-programming/cultist-wars`.
Account used previously: `hexagonee`.

Last saved completed arena snapshot (`validation/lmr/cg-progress.json`): submission **41709358**, Gold **#3**, overall **#22/400**, score **24.91**; boss **26.92**, promotion false. Historical snapshot, not live status. Earlier pre-consistency score 25.62; no established arena gain from latest submitted changes.

Two distinct sources:

- Submitted: `submissions/41709358.c`; SHA-256 `82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d`.
- Current local: `bot.c`; SHA-256 `ce733367331be5c5b19b5ce68ac7b97f9ada53b681339398edd54358982d8f79`.
- Current local matches `validation/strength/q-lazy-eval.c`. Only lazy Q evaluation + ablation switch differ from strength baseline, which matches submission 41709358.
- That advice ("do not submit yet": speed-only change, no proven strength gain) held until the tuning pass. **Superseded by §9**: `bot.c` now also drops the flat leader bonus, which does have a seat-paired gain, and is cleared for submission. Hashes in §9 are current; the two bullets above are the pre-tuning state.

## 2. Chronology / decisions

1. Installed CodinGame agent skill, authenticated, read rules. Cloned Java referee into `source/` from `https://bitbucket.org/Nixerrr/cultist-wars/src`; initial clone revision `bf7d35e`. Reference files later tracked in root repo. Initial account Bronze; progressed through Silver to Gold.
2. Built C bot: exact Bresenham shooting, simulation, minimax/alpha-beta, heuristic evaluation, neutral RNG inference. Initial archived submission **41708638**.
3. Collected one public replay per initial top-10 leaderboard player. Saved text, JSON, index under `replays/top10-2026-11-13/`. Samples, not full player histories.
4. Studied MSz first-place postmortem. Added iterative deepening/PVS/aspiration/TT, tactical Q, guarded futility, cheap BFS evaluation. Submission **41708923**. Local vs initial bot: **10W/3D/3L**; small regression sample.
5. Throughput work: packed states, occupancy bitboards, incremental Zobrist, ray lookup/masks, eval/Q caches, precomputed integer terms. Submission archives **41709087**, **41709130** preserve intermediate versions. Fixed-depth local speedup ~2.78x vs older engine; sampled judge midgames exceeded 120k mean visits. Visits != unique states or Monte Carlo rolls.
6. Wrote `RESEARCH.md`: postmortems, alpha-beta/learning/pathfinding options, experimental methodology. Historical research snapshot; some listed gaps later fixed.
7. Consistency work: bounded quiet history; threat-aware Q searches all legal replies for immediate kill/conversion threats; true terminal count scoring near action limit; faster exact threat detector. User explicitly requested **49 ms**. Deadline checks tightened to every 32 visits. Submitted **41709358**. Local holdout **17W/6D/9L** vs incoming engine; final selected boss replay 4–3 win. Later completed arena snapshot remained Gold.
8. Isolated conservative LMR. ~5.79% fewer visits, ~4.01% elapsed saving, modest nominal-depth gain. Combined local **18W/11D/19L**. Four private boss seeds: baseline **2W/2L**, LMR **1W/3L**. **Not promoted**; `validation/lmr/` only.
9. (see also §9) Latest decision-quality work reconstructed replays independently with Java; diagnosed errors; tested HP tables, recruitment arrival, lazy Q eval. HP rejected, recruitment inconclusive. Retained only equivalent-result lazy Q optimization. **Not submitted**.

## 3. File map / reading order

- `CLAUDE.md`: continuation instructions.
- `bot.c`: entire production bot, ~660 lines; no external runtime dependencies.
- `README.md`: implemented behavior, build/test instructions; earlier chronological sections retain historical claims.
- `Makefile`: C11 build, correctness/protocol checks, sanitizers.
- `source/config/statement_en.html`: supplied game statement.
- `source/src/main/java/com/codingame/game/Referee.java`: referee authority.
- `source/src/main/java/model/`: exact game/map/RNG rules. Preserve supplied source.
- `tests/test_bot.c`: exhaustive rays, transitions, minimax, RNG, threat/history/endgame regressions.
- `tests/test_protocol.py`: subprocess protocol/legal actions; latency diagnostic.
- `tests/test_differential.py`: historical-reference transitions, incremental-cache consistency, compatible fixed-depth values.
- `tests/benchmark.py`: local C simulator, symmetric maps, paired seats, `--seed-start` support.
- `tests/summarize_judge.py`: saved judge log metrics.
- `submissions/`: source + JSON metadata per submission; immutable baselines.
- `validation/msz/`: initial search experiment, benchmark, submission/progress logs.
- `validation/throughput/`: original representation/cache speed work; README predates 32-visit deadline checks and threat-aware Q.
- `validation/consistency/`: frozen incoming engine, history/threat variants, real replay tests, timeout evidence, selected49 holdout/submission.
- `validation/lmr/`: isolated reductions, instrumentation, fixtures, baseline, private boss tests, last saved completed progress.
- `validation/strength/`: latest authoritative experiment report, Java reconstruction, tactical corpus, candidates, holdouts, profiling, checksums.
- `RESEARCH.md`: future hypotheses + references; not proof of implemented features or strength gains.
- `replays/top10-2026-11-13/README.md`: replay index and sampling limits.

Useful current `bot.c` landmarks: state/cache helpers lines 41–114; initialize 142; rays 205–230; terminal/eval 231–278; move generation 279; RNG/transitions 319–372; threat detector 380; lazy Q switch/search 420–465; main search 466; iterative choose 539; observation tracking 564; protocol main 617. Verify locations after edits.

## 4. Rules / engine invariants

- Board 13×7; 14 initial units; one action per player turn; 150 total actions. Judge limit 50 ms.
- Win by elimination or surviving unit count at horizon. Leader death alone does not lose. HP cannot break terminal count ties.
- Neutral conversion changes count differential +1; enemy conversion +2; kill +1.
- Shot damage `7 - Manhattan distance`, range 6. First Bresenham blocker receives shot; intended target can differ from victim, including friendly/neutral victims. Exact strict ties/lower-Y traversal matter.
- Neutral movement follows every action. Separate 31-bit LCG starts at 42, independent of map seed. Neutral-list membership/order, dead entries, absent-index draws, trapped-unit draws, corpses before cleanup all matter.
- Observation tracker matches legal opponent transitions. Predict only with one consistent RNG candidate; ambiguous/mismatched state uses stationary-neutral search fallback. Mismatch disables prediction for rest of game.
- Full search hash includes ownership, HP, positions, side, remaining actions, RNG state/knowledge, neutral membership. Board-only eval cache stores player-zero-relative score; terminal checks first. New side/turn/RNG-dependent eval features require cache-key changes or split caching.
- Search: iterative-deepening negamax alpha-beta, PVS, widening aspiration, 65,536-entry TT, bounded quiet history. Four-ply Q; all legal replies when immediate material threat or terminal horizon requires. Separate Q cache.
- Guarded depth-one null-window futility remains selective. Stand-pat outside unstable/horizon cases remains heuristic. BFS ignores occupancy. Evaluation still hand-set unit count, linear HP, BFS proximity; the flat leader bonus is now 0 (`LEADER_BONUS`, §9), so leader value comes only from the proximity term.
- `Q_LAZY_EVAL=1` default skips unused static eval in unstable Q nodes. `-DQ_LAZY_EVAL=0` ablates. No altered legal actions/leaf values.
- 49 ms deadline starts after unit-count read, includes parsing/tracker work; initial map setup precedes it. Every 32 visits checked. Actual own timeouts occurred at 49 and 45 ms; user still selected 49 ms. Do not silently change budget or claim safe margin.

## 5. Latest evidence / failed paths

Read `validation/strength/README.md` in full before changing strategy.

Independent model validation: **17 saved games / 2,207 transitions**, Java vs C exact matches. Eight counterfactual branches add **128** matching transitions. Java adapter validates model, not judge scheduling/timeouts.

Ranked observed problems:

1. Premature exchanges / missed quiet escapes: three of four screened games. Examples: Lv0Sys action 28, boss-loss action 60.
2. Indirect-shot victim / exchange choice: boss seed 2000 action 32.
3. Recruitment sequencing / one-action opportunity cost: earlier boss loss action 18.
4. Leader retreat / recruitment capability: seed 2001 actions 40/54.

Use `replay-positions.json`, `corpus.json` (14 fixtures), `labels.jsonl`, `counterfactual.jsonl`. Initial 500 ms `diagnosis.jsonl` gaps have mismatched horizons: screening only. Matched-horizon labels still share baseline evaluation, futility, bounded Q; not ground truth. Counterfactual follow-up can erase apparent advantage.

Rejected/inconclusive:

- `eval-hp.c`: hand-made HP thresholds, no chosen-action changes in 14 replay fixtures; development **4W/4D/8L**; boss **2W/2L** vs baseline **2W/1D/1L**. Rejected.
- `eval-race.c`: side-aware recruitment arrival margin; development **8W/2D/6L**. One improvement, one opening regression of 1,150 reference points. Inconclusive, not promoted. Eval miss cost +24.1% in microbenchmark.
- LMR: not promoted; no repeat without new causal evidence.

Retained lazy Q optimization:

- **177/177** depth-six comparisons match value/action/visits/Q visits/stop status: 59 fixture entries × three repeats, not 177 independent positions.
- Both visit **85,510,374** nodes; elapsed **9.136596 → 8.805686 s**, **3.62% less**.
- Final local seeds 4000–4015: **10W/10D/12L**, 46.88%, paired exploratory 95% interval 39.06–54.69%.
- Final boss seeds 4100–4103: candidate **3W/0D/1L**, baseline **3W/1D/0L**. One seat only.
- Older tactical-only opponent, seeds 4200–4203: both variants **3W/3D/2L**.
- No demonstrated equal-time strength gain. Instrumented profile itself slightly slower; performance claim uses uninstrumented repeats.
- Q dominates measured fixed-depth workload: latest profile ~97.66% visits / 95.28% profiled time. Earlier LMR corpus ~97.23% / 91.20%; workload differs.

Correctness, ASan/UBSan, differential checks previously passed. Differential: 720 states, 21,708 transitions, 24 compatible fixed-depth results. Latest protocol logs include first-response startup outliers 238–256 ms; subsequent responses ~49 ms. Not equivalent to confirmed judge timeouts, not proof of timing safety.

## 6. Reproduce / continue

Prereqs: C11 compiler, make, Python 3 standard library; JDK (`javac`, `java`) for independent reconstruction. No Python dependency install needed for listed harnesses.

From repository root:

```sh
make
make test
make sanitize
python3 tests/test_differential.py
python3 validation/strength/verify.py
python3 validation/strength/check_ablation.py
python3 validation/strength/compare.py fixed --candidate q-lazy-eval.c
```

Historical comparison reproduction (seeds already used; not fresh holdout):

```sh
python3 tests/benchmark.py --baseline validation/consistency/baseline.c --pairs 12 --seed-start 200 --budget-ms 35
python3 validation/strength/compare.py matches --candidate q-lazy-eval.c --seed 4000 --pairs 16
```

Read script before running generators/reconstruction: some regenerate saved artifacts. Preserve original JSON/logs; use new output filenames/experiment dirs. Avoid concurrent CPU-intensive jobs during timing. No tests rerun for this documentation-only handoff; results above are preserved prior runs.

Next work:

1. Freeze current local source and submitted baseline separately. Confirm hashes/status.
2. Expand independent replay/outcome data; split by whole games/map seeds. Existing holdouts now used; reserve fresh seeds.
3. Target documented exchanges/escapes/recruitment errors. Test contextual leader value, smooth horizon weighting, or fitted cheap evaluation only with causal evidence and ablations. Do not retry rejected hand-made HP/race features unchanged.
4. For speed, profile Q generation/ordering/cache/transition residual. Prefer exact-result improvements; preserve threat/terminal regressions. TT replacement/staged generation secondary, measured hypotheses.
5. Validate one change at a time: tests/sanitizers, tactical choices, parity where applicable, equal-time paired seats, diverse opponents, uncertainty, timeouts, evaluation cost. Depth/visits diagnostic only.
6. Promote only evidence-supported candidates. Keep negative experiments. Current speed-only change insufficient submission justification.

## 7. CodinGame access / portability

Installed skill outside repo: `/Users/hexagone/.codex/skills/codingame/SKILL.md`; read before API use. Original discovery URL: `https://www.codingame.com/api/v1/docs/llms.txt`. Existing helpers: `validation/lmr/cg.py`, `validation/strength/private_tests.py`.

Helpers read local token at `~/.config/codingame/token`; **do not print, commit, or include token in handoff**. New machine: authenticate through documented flow. Skill/API instructions and credentials do not automatically transfer with Git.

User authorized private Gold-boss tests in latest work. Earlier consistency task authorized tests + improved submission; latest research tasks explicitly kept arena submission out of scope. Handoff authorizes no new upload/submission itself. Preserve this distinction. Last answer advised withholding current candidate.

Private tests use `/api/v1/puzzles/cultist-wars/test-games`, language `C`, source code, opponents `['boss']`, explicit seed. Poll returned test ID; obey returned limits, which have varied across runs. Helpers do not mutate IDE or submit arena code. Some batch helpers skip existing files; use fresh prefix/seeds. Historical limits in JSON are stale.

## 8. Git / transfer warning

Only two existing commits: `b3963d1` initialization; `29afabf` reference-source tracking. Most bot evolution, tests, research, submissions, replays, validation are **uncommitted**. Git clone alone will not transfer current project state.

At handoff start: tracked edits `README.md`, `bot.c`, `tests/test_bot.c`; untracked `RESEARCH.md`, `replays/`, newer submission pairs, benchmark/differential/judge scripts, entire `validation/`. This handoff adds `CLAUDE.md`, `HANDOFF.md`.

Use same folder in Claude Code, or copy full workspace including untracked artifacts. Do not reset/clean/overwrite existing work. Binaries `bot`, `tests/test_bot`, Java target, Python caches ignored and rebuildable. Credentials remain outside repo.

Commit policy update: commit-message hook and GitHub validation removed. Keep commits focused; no enforced message format. Earlier no-commit statements describe the historical handoff.

## 9. Tuning pass — leader bonus removed (2026-11-13, after §8 of RESEARCH.md)

- Current local `bot.c`: SHA-256 `cd2897ab87048d84bd3df65ec6d2419b33ba740ec8515c5cb40a190d0787f8cc`.
  Pre-change source frozen at `validation/tuning/pre-leader-bonus.c`
  (`ce733367…`, = submitted 41709358 + lazy Q).
- Change: flat leader bonus 420 → 0, exposed as `LEADER_BONUS` (default 0).
  `-DLEADER_BONUS=420` restores pre-tuning leaf values; `tests/test_differential.py`
  now builds the reference that way and still passes exactly.
- Evidence: `validation/tuning/README.md`. Seat-paired, real maps, equal time:
  53.23% at 20 ms (240 pairs) and 51.94% at 49 ms on disjoint maps (296 pairs),
  pooled 52.52% (CI 50.88–54.16). Control reads 49.62%. Dose–response peaks at 0.
  Treat the gain as ~+2 points. Late-only discount gained nothing (50.00%).
- Gate for all future candidates: `tests/arena_match.py` (see its docstring).
  ~250 games per 3 minutes on 4 cores at 20 ms; ~120 at 49 ms. Always both seats.
- **Not done: no private boss test, no arena submission.** The token at
  `~/.config/codingame/token` was outside the folders the session could read.
  Run from the repository root on the machine holding the token:

  ```sh
  python3 validation/tuning/cg_submit.py --boss     # seeds 5000-5003, both orders
  python3 validation/tuning/cg_submit.py --submit   # submits bot.c, polls, archives
  ```

  `--submit` writes `submissions/<id>.c` and `submissions/<id>.json` and saves the
  final progress to `validation/tuning/cg-progress-after.json`. Boss games are
  informational only: 8 games cannot resolve a 2-point change.
- Nothing committed. `bot.c`, `README.md`, `RESEARCH.md`, `tests/test_differential.py`
  are modified; `tests/arena_match.py`, `validation/tuning/`, `validation/arena-verify/`
  are new and untracked.
