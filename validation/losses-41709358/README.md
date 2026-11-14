# Submission 41709358: loss-focused study

Study date: 2026-11-14, Africa/Casablanca. Scope is archived submission **41709358**, agent **6880547**, source `submissions/41709358.c`, SHA-256 `82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d`.

Live read returned current submission **41710886**, 78% ranked, Gold #3 / global #22, score 25.03; boss 27.26. This is a transient snapshot, not 41709358's result. Test-game quota exhausted; no test games or submissions started. API battle list explicitly requested `submission=41709358`: 91 games, no further page. All 91 were already in the local arena archive, so replay study required no additional game requests.

## Exact sample

54 wins / 14 draws / 23 losses. Seat 0: 32/5/6 in 43 games; seat 1: 22/9/17 in 48. Seven wins involve opponent timeout/failure scores; none of the losses does. Excluding those seven, 47/14/23. Counts are a historical submission sample, not a controlled strength estimate.

Highest relevant opposition: boss 3/0/5, Lv0Sys 4/1/3, YALOKGAR 2/0/3. Current leaderboard has Lv0Sys Gold's highest-ranked human; YALOKGAR is immediately below us. These ranks are contextual snapshots, not necessarily their ranks at battle time. N3l: 2/1/0, so no lost game to dissect here. This submission did not play Legend's top opponents in this sample.

Reconstruction: all 91 games legal under supplied unmodified Java model; 12,163 actions reconstructed. Final counts agree in all **84 non-timeout games**. Seven timeout games have referee-assigned failure scores rather than final-count scores. This is model/replay validation, not judge-timing validation.

## Where losses accumulate

Across all 23 losses, unit-count changes attributable to neutral recruitment and combat:

| Actions (zero-based) | Net recruits | Net owned-unit kills |
|---|---:|---:|
| 0–39 | -3 | -2 |
| 40–99 | **-27** | **+4** |
| 100–149 | -5 | -15 |

Across just the 11 boss/Lv0Sys/YALOKGAR losses: midgame recruits **-17**, kills **+4**. We often trade or win combat while opponents convert the remaining board. This reproduces the research's recruitment observation on the exact requested submission. It does **not** prove that adding a recruitment bonus improves decisions.

## Individual top-opponent losses

Counts below always **ours–opponent**. “Permanent deficit” is the first state after which the recorded game never returned to equal counts; it is not a proof of an unavoidable loss. Actions are zero-based; state 39 follows action 38.

| Game | Opponent | Seat | At action 40 | Final | Permanent deficit | Decisive recorded pattern |
|---|---|---:|---:|---:|---:|---|
| 921211508 | Boss | 1 | 3–4 | 3–7 | 39 | Cultist 12 trades down; boss recruits 8/13 at 48/50, then 3/4 at 78/102. |
| 921210319 | Boss | 1 | 4–5 | 1–4 | 55 | Boss takes neutral 7 at 54, kills cultist 9 at 56; our shot kills neutral 12 at 59. Leader dies at 88. |
| 921207904 | Boss | 1 | 4–5 | 3–5 | 67 | Boss converts neutral 12 at 28 and 3 at 52; our combat kills do not replace recruitment. Cultist 8 dies at 66, boss takes 10 at 74. |
| 921208877 | Boss | 0 | 3–3 | 1–2 | 56 | New recruit 6 immediately absorbs five damage, retreats twice, dies at 21. Later leader kill at 50 and cultist kill at 52 still leave cultist 5 dying at 55; lone leader cannot recover. |
| 921210462 | Boss | 0 | 5–4 | 2–3 | 122 | Long conversion ping-pong over unit 3; boss resumes recruiting. Later chasing/killing boss leader does not secure a win. Cultist 5 loses a shootout at 121. |
| 921211685 | Lv0Sys | 1 | 5–5 | 4–6 | 49 | Equal exchange at 43/44; opponent converts 3/9/6 at 48/50/64. Our next recruit arrives at 97. |
| 921210755 | Lv0Sys | 1 | 6–5 | 5–6 | 105 | Initially ahead; opponent equalizes recruitment. Six each at 100, our 1-HP leader dies at 104. Deeper local search still chooses the same preceding plan: not an established simple escape fix. |
| 921209667 | Lv0Sys | 1 | 6–6 | 4–5 | 73 | Several shots kill neutral 10 by 37; opponent later takes last neutral 7 at 72. Our cultist 12 dies at 78. Action 69 is the strongest surviving local counterfactual. |
| 921211936 | YALOKGAR | 1 | 4–5 | 4–5 | 51 | Equal combat exchange at 33/36, opponent recruits at 38 and wins another recruit at 50 immediately after ours at 49. |
| 921210601 | YALOKGAR | 1 | 4–5 | 3–6 | 45 | Cultist 8 dies at 28; opponent takes 7/13 at 44/48. Subsequent trades fail to recover count. |
| 921208414 | YALOKGAR | 0 | 4–3 | 1–5 | 104 | Early lead erodes. Low-HP cultists die at 95/103 while enemy leader collects 8/13/12 late. Apparently strong quiet escapes fail longer continuation checks. |

## Counterfactual tests: what survived

71 selected positions screened with the **frozen submission**, not current bot.c. Root search depth 6 generates alternatives; each played/alternative action is independently rescored with a fresh cache at child depth 5. Same horizon, same evaluation, exact reconstructed neutral RNG. Bounded quiescence/futility still apply. These are selected positions, not unbiased error-rate estimates.

Four differences over 400 points were followed for 16 plies, both branches, with fixed-depth-5 replies. All **128 branch transitions** matched independent Java replay. Continuation opponents use our engine, not the inaccessible opponent code. Fresh-cache exact-RNG analysis differs from live tracker state, retained history, judge CPU and search timing.

| Game / action | Played → alternative | Depth-6 score gain | Counts after 16 plies (ours–opponent), played → alternative | Verdict |
|---|---|---:|---|---|
| 921209667 / 69 | `1 MOVE 8 3` → `8 MOVE 10 5` | +885 | **5–7 → 6–7** | Best surviving diagnostic: activate supporting fighter rather than spend another leader move. Still losing, not proven saved game. |
| 921210319 / 85 | `11 SHOOT 5` → `8 MOVE 9 5` | +411 | 2–4 → 2–5 | Reject as improvement claim. |
| 921208414 / 94 | `7 MOVE 3 1` → `2 MOVE 2 2` | +971 | 2–3 → 2–3 | Horizon illusion; paths converge. |
| 921208414 / 102 | `2 SHOOT 9` → `2 MOVE 2 2` | +950 | 2–4 → 2–5 | Escape postpones death while enemy recruits; reject. |

Three fresh 49-ms runs reproduced the recorded move at the three rejected positions, reaching depths 8–10. At Lv0Sys action 69 all three instead selected **`8 MOVE 9 6`**, completed depth 5. This supports investigating support activation/search stability but does not isolate the cause of the recorded leader move. That third move was not branch-tested here.

Additional concrete trace: in 921209667, actions 31/33/35/37 each damage neutral 10 by 2; action 33 aims at leader 0 but hits the neutral blocker. Our depth-6 engine still chooses the recorded shots at 35/37. Hence this is not evidence of a simulator bug or automatically wasted fire. Test denial benefit versus time cost before suppressing neutral shots.

## Improvement priorities

1. **Build a regression gate around these actual losses, especially second seat.** Start with action 69 of 921209667 and earlier combat/recruitment decisions in the three seat-1 boss losses. Preserve same map, seat and opponent where replay access permits; quota expiry alone does not guarantee old opponents remain replayable. Include wins/draws as controls and fresh held-out maps.
2. **Investigate leader activity versus fighter support, not raw race weight.** Candidate features: safe recruit access, damage/tempo cost of the path, supporting fighter activity. First isolate action 69's live-vs-reconstructed disagreement by running full prefix with observation tracking and persistent search history; check selected action, completed depth and RNG certainty. Then test a small isolated candidate. No cause established yet.
3. **Treat neutral shooting as context-dependent denial.** Count the actual blocker/victim. Assess whether opponent can recruit it soon and what our leader could accomplish in those turns. A blanket ban is unsupported.
4. **Require longer branch checks before labeling tactical blunders.** Three attractive depth-6 alternatives failed already. Quiet evasions and immediate-threat Q exist in 41709358; simply adding them again is not a fix. Test whether a proposed search change resolves multi-turn plans without spending the 49-ms budget on irrelevant branches.
5. **Do not repeat disproven research changes.** RESEARCH.md §7 corrects the original static-fit conclusion: search values predict much better than raw eval. Large race weights lost badly; HP 60 and LMR lacked gains. Flat leader-bonus removal won self-play modestly but its later arena submission fell from 24.91 to 23.85. Different pools/timing prevent causal certainty, but it cannot be called a validated solution to these losses.

No production source changed, no candidate promoted, no claim of proven extra arena wins. This study narrows the next experiment and retains negative counterfactuals.

## Artifacts / reproduction

- `progress.json`, `leaderboard.json`, `battles.json`: authenticated read-only snapshots.
- `games.json`: exact 91-game subset of cached archive.
- `study.py`: legal Java reconstruction, events, phase statistics; writes `positions.json`, `rows.json`, `reconstruction.txt`.
- `diagnose.py`, `diagnosis.jsonl`: 71 matched-horizon tests.
- `branches.py`, `branches.jsonl`: independent 128-transition verification and three 49-ms repeats per selected position.

Run from repository root: `python3 validation/losses-41709358/study.py`, then `diagnose.py`, then `branches.py` at the same path. Scripts overwrite their own derived outputs; preserve these snapshots before rerunning. Requires Python, C compiler, JDK. No network calls in these scripts.
