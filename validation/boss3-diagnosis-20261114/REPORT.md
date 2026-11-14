# BOSS 3 loss diagnosis — 2026-11-14

Artifacts: `validation/boss3-diagnosis-20261114/` (scripts, raw outputs, this report as `REPORT.md`).
No submission, no IDE write, no test game started. Five read-only API GETs (progress, opponents, limits, submissions, code).
Production `bot.c`, `submissions/`, and all earlier `validation/*` evidence untouched.

## 0. Bottom line

BOSS 3 does not out-calculate us. With our frozen engine sitting in BOSS's seat at 2,074 of its real decisions, it picks the same move 57% of the time, most disagreements are near-ties, and BOSS's large deviations *lose* it material. What BOSS does differently is slow and positional, and our engine scores it as a tie:

1. **Its leader keeps uncontested access to more neutrals.** Leader 1.0–1.8 cells further forward by turn 20–30, 1.2–2.4 more neutrals in its sphere by turn 20, in both seats, in our wins and our losses.
2. **Our results hinge on whether that leader gets hurt early.** BOSS leader at 6 HP or less by turn 30: we score 80% as first seat and 67% as second. At 9 HP or more: 46% and 8%.
3. Our evaluation prices leader HP like cultist HP (18/HP), gives a leader step toward a neutral about 10 points, and gives neutrals no value until a conversion is inside the 5–6 ply horizon. So contesting access, zoning the leader, and denial all look like ties, and ties are broken by weak proximity gradients.

This is an association plus a mechanism, not proven causation. Earlier static fixes in this direction (race weight, symmetric leader-HP weight) lost in self-play, and no candidate has ever been tested against BOSS with more than 16 games. §6 is the experiment that would settle it.

## 1. Versions and chronology (verified by hash)

| Submission | UTC | SHA-256 (prefix) | Content |
|---|---|---|---|
| 41709358 | 11-13 18:16 | 82b341ea | Threat-aware Q, history, 49 ms. Leader bonus 420. |
| 41710268 | 11-13 21:29 | cd2897ab | Leader bonus 0 + lazy Q. **Equals working `bot.c`.** |
| 41710692 / 41710835 | 22:55 / 23:23 | 31098fa7 / 3196ba70 | `bot_x` LEADER_BONUS=420:LPROXE=50 / `bot_y` LSTEP=20 |
| 41710886 | 23:35 | (= 41709358 source per `submissions/41711081.json`) | Revert to 41709358 |
| 41711081 | 11-14 00:28 | 5f1dbaed | recruit-Q |
| 41711135 | 00:54 | 902527dc | 41709358 + 4,161-parameter root-ordering net. Diff verified: only NN block + 3 hook lines. Current IDE code. |
| **41711350** | **02:46** | a722bde9 | 41711135 with 24,833-parameter root net. **Live arena submission now, 13% ranked at 03:05.** Submitted by another session while this study ran. |

- `bot.c` (cd2897ab) is the 41710268 lineage: leader bonus 0, no NN. Everything labelled "41711135" (second-seat, horizon, nn-large baselines) concerns 902527dc, which has leader bonus 420. They differ in evaluation.
- BOSS 3 = agent 4611894: confirmed live (`api-readonly-snapshot.json`) and in every one of 245 saved logs naming a boss. Rating 27.42.
- Test-game quota at 03:05 UTC: 199/200 used in the 6-hour window.

Corpus: 225 distinct BOSS 3 games (`boss_games.json`), 68 distinct map+seat pairs. 2 games with timeout scores excluded from statistics. `race2/boss/_invalid/` not read. "Baseline family" = 41709358, 41710268/cd28, ce73, 41711135: 92 games, 60 distinct map+seat pairs. The 16 nn-large boss games finished after the snapshot; only one is included.

## 2. Current approach, and what it can and cannot see

| Component | What it does (41709358 line numbers) | Strategic assumption |
|---|---|---|
| State / rules | Packed units, exact Bresenham, exact neutral LCG (`apply` 345, `move_neutral` 324) | Verified here: 31,120 transitions of all 225 boss games identical to the Java model, RNG state included. |
| Hidden RNG | `track_observation` 568 | Singleton on 5,308 of 5,327 logged turns; the other 19 are first turns. No tracker failure in boss games. |
| Evaluation | `evaluate_live` 252: 1000/unit + 18/HP + leader 420 (0 in `bot.c`) + leader Σ100/(1+d) to neutrals and enemy cultists + 240/(d+1) nearest + fighter 90/(d+1) nearest | Material now. Neutrals are worth nothing until converted. Leader HP = cultist HP. One leader step toward a neutral 5 away ≈ +10; a 2-damage shot = +36. |
| Search | ID negamax, PVS, aspiration, 64k TT, history, depth-1 futility (466–537) | Completed depth in real boss games: 8.5 (t<20), **5.3 (t 20–39), 5.8 (40–59)**, 6.4, 7.1, 7.4. |
| Quiescence | 4 plies; shots with positive order + converts; full replies under material threat (420–465) | Sees immediate kills and conversions only. |
| NN | Root move ordering only | In-game move equals fresh local 49 ms move 81.5%; NN on/off changed 1/56 choices (prior study). No evidence it matters. |
| Time | 49 ms from first read, check every 32 nodes | 14 of 5,327 turns logged above 50 ms; no timeout loss in the corpus. |

Search can discover: anything material within about 3 own actions. Evaluation must already know: who will get the neutrals that are 3+ leader steps away, what an enemy leader standing among neutrals is worth, what chip damage on a leader or a contested neutral is worth. It knows none of these.

## 3. Audit of earlier conclusions

| Claim | Verdict | Evidence here |
|---|---|---|
| Rules / tracker bug | **Rejected** | `check_eng.py`, `logs.py` |
| "Second-seat specific weakness" | **Not established** | First seat ≈ parity: 53% on 48 fresh seeds, 64% in 11 arena games. Second seat 15% in 10 arena games (the only unselected sample); replays 9–23% reuse 16 maps picked from losses. With the arena's 69% first-seat edge, a uniform deficit of ~0.6 logit predicts roughly this split. |
| Midgame recruitment deficit | **Reframed** | First seat: total neutral conversions are equal (4.99 vs 4.92); BOSS just converts later. Second seat: BOSS +2.1 neutrals (6.48 vs 4.40), won at conversions #4–#6 around turns 18–36, not "40–100". |
| Tactical alternatives fail longer checks | **Confirmed, generalised** | By depth-6 judgement our in-game moves are ≥300 worse than best in 2.4% of 1,462 decisions, no higher in losses (2.2%) than wins (3.3%). |
| Horizon reversals exist; extension too costly | **Confirmed, but not the cause** | Depth-6 best is ≥300 worse at depth 8 in 5.8% of 428 decisions (t 13–45). Same rate in wins (9.1%) and losses (7.1–8.0%). Only 8 of 27 sampled losses contain one. |
| Root NN barely changes choices | **Consistent** | See §2. |
| Self-play gains do not transfer | **Consistent** | Leader bonus 0: +2.5% self-play, arena 24.91 → 23.85. Not re-measured. |
| Larger race weights lose | **Consistent**, plus one unreported result | `race/s1`: symmetric leader-HP weight `LHPW=40` scored **40.1%** in self-play (67/57/114) vs control 51.7%. It was listed as "not run". |
| BOSS is tactically stronger | **Rejected** | §4.2 |

## 4. Evidence

### 4.1 Behaviour, same games, baseline family (us / BOSS)

| | Our seat 0 (67 games) | Our seat 1 (25 games) |
|---|---|---|
| Leader distance from start, t=20 / t=30 | 4.4 / 4.9 vs **5.5 / 6.0** | 4.1 / 4.4 vs **5.3 / 6.2** |
| "Territory" at t=20 (own cultists + neutrals nearer own leader) | 5.14 vs **6.38** | 4.56 vs **6.96** |
| Neutral conversions t<40 / 40–99 | 4.34 / 0.58 vs 3.70 / **1.13** | 3.76 / 0.56 vs 4.44 / **1.68** |
| Shots hitting enemy leader, t≥100 | 0.15 vs **1.01** | 0.20 vs **0.72** |
| Neutrals killed (from t=20) | 0.30 vs **0.60** | 0.28 vs **0.60** |
| WAIT actions per game | 2.2 vs **0** | 2.3 vs **0** |

Territory is already −0.7 to −1.0 at t=12. It is the same in games we win (−1.0 at t=20) and lose (−1.7), so early territory alone predicts little (r ≈ 0.15 at t=20, 0.49 at t=40). Wins are the games where it recovers between t=20 and t=40.

What separates wins from losses is BOSS's leader HP (all 219 valid games of our engine family):

| BOSS leader HP at t=30 | Our score, seat 0 | Our score, seat 1 |
|---|---|---|
| ≥ 9 | 46% (n=108) | 8% (n=55) |
| 7–8 | 50% (n=7) | 6% (n=8) |
| ≤ 6 | **80% (n=35)** | **67% (n=6)** |

Same map and seat, different runs: the run with the more damaged BOSS leader scored better in 47 pairs, worse in 22, equal in 58 (pairs are not independent). Our own leader's HP at t=30 does not predict our score (53–58% in every band, seat 0).

We do not decline leader shots more than BOSS does: when a shot on the enemy leader is legal we take it 21–28% of the time, BOSS 25–34%. The difference is how often a cultist is standing where the shot does real damage.

### 4.2 BOSS's decisions seen through our engine (`bosspos*.jsonl`, `cells.py`, `blunders.py`)

Frozen 41711135, exact RNG, fresh caches, one game per distinct map, t = 6–112.

- Same move as BOSS: 60.7% (t<46), 52% later. Legend players were 42–46% in the earlier study, so BOSS is much closer to our engine than Legend is.
- Disagreements: median depth-6 gap 4–25 points; 64–68% under 50.
- In identical positions BOSS shoots 35.7% of the time, our engine 30.0%; BOSS never waits.
- BOSS moves our depth-6 search rates ≥300 below best: 7.1%. After them BOSS's unit balance falls 0.4 within 12 plies and stays down at 40 plies. They are real errors, not sacrifices.
- After BOSS moves that match our engine, BOSS gains (+0.13 units in 12 plies); after every class of deviation it loses (−0.11 to −0.26).

So at the level our search and evaluation can measure, BOSS is no better than we are. Its edge is in what they cannot measure.

### 4.3 Three positions

Unit ids hex, `*` leader. Coordinates (x,y).

**A. Loss, second seat, 41711135, arena game 921374717 (0–3).** `nn-41710886/arena/game-921374717.txt`

```
t=17, us to move, 4 v 4            us:   *1 (10,2)  8 (11,2)  a (9,1)  d (9,2)
.#..#...#..#.                      boss: *0 (2,2)   2 (1,2)   4 (2,1)  7 (3,3)
..4......a...                      neutrals: 3 (5,5) 5 (4,6) 6 (2,6) 9 (6,4) b (7,4) c (9,5)
.20......d18.
..#7......#..
.#..#.9b#..#.
.....3...c...
..6.5#.#.....
```

BOSS's leader walks five steps alone into the centre (t=18–26), then converts 3, b and 9 at t=30, 34, 36 while at 7→5 HP. Its three cultists never leave x ≤ 3. Our leader walks to c and stops at (9,4) from t=23, two steps from b. Our four cultists stay at x ≥ 8, y ≤ 2 and hit the leader three times for 3, 2, 2.

Our logged root value: +1114 (t=21), −78 (t=25), −1006 (t=29), −1690 (t=37), at completed depth 5, 5, 6, 5, 4. Fixed-depth re-analysis (`pivot.py`):

| Turn | Played | Depth 6 says | Depth 8 says |
|---|---|---|---|
| 21 | `1 MOVE 9 3` | best, +1044 | −1; `13 SHOOT 0` (the leader) +1112 |
| 23 | `1 MOVE 9 4` | best, −1 | −78; `13 MOVE 7 2` +504 |
| 25 | `13 SHOOT 0` | best, −78 | −1070; `1 CONVERT 12` −124 |

Upstream decision: t=17–23, not contesting the three-neutral cluster and not bringing a cultist onto the leader's lane. Visible symptom: three conversions at t=30–36. Depth 8 is a heuristic continuation with our own evaluation, not ground truth.

**B. Loss, first seat, 41709358, test game 921222750, seed 2000 (1–5).** `strength/boss-baseline-2000.json`

```
t=38, us to move, 3 v 3            us:   *0 (2,3)  5 (2,4)  7 (3,4)
......3......                      boss: *1 (9,2)  a (8,1)  d (7,3) hp6
........a9...                      neutrals: 2 (2,6) 3 (6,0) 8 (10,6) 9 (9,1) b (9,4)
.#.......1.#.
.#0....d...#.
.#57#...#b.#.
##..#...#..##
#.2.......8.#
```

After an even exchange, BOSS's leader has three neutrals within reach on the right, ours has one. BOSS converts at t=41, 51, 61. We convert neutral 2, three steps away, at t=60 after leader moves at t=36, 46, 50, 58. Our three units never cross x=4. BOSS's leader takes zero damage before t=60; a shot on it was legal on three of our turns, for 1 damage.

Our root value is flat at −1190 to −1260 from t=34 to t=62. At t=36, 38, 40 the top four moves at depth 6 and depth 8 are within 30 points of each other, and 3-second searches (depth 11) change nothing. No horizon error and no misranked tactic: the engine sees one unit down, sees no way to affect it, and has no gradient that sends cultists across the board. Of 13 first-seat losses with three or more BOSS conversions in t 18–70, BOSS's leader was untouched before t=60 in 9.

**C. Matched win, second seat, 41711135, replay of 921338102, test game 921367944 (4–3).** `nn-41710886/boss5-second/`

```
t=21, us to move, 5 v 4            us:   *1 (11,1)  8 (12,1)  9 (11,0)  a (9,3)  d (11,4)
3...#..c#..9.                      boss: *0 (5,3)   2 (1,1)   4 (2,2)   7 (1,4)
.2##.6...##18                      neutrals: 3 (0,0) 5 (3,6) 6 (5,1) b (8,5) c (7,0)
..4.#...#....
.....0...a...
.7.........d.
..#.....b.#..
...5.#.#.....
```

Same BOSS plan as position A: leader alone into the centre. Here our cultist a had stepped onto the open row at t=19 and shoots the leader at t=21, 25, 27, 31, 33 (3, 3, 2, 2 and the kill). BOSS converts no neutral between t=18 and t=70. In three of the four second-seat wins in the corpus, BOSS's leader is at 2 HP or less by t=30.

A and C differ by one cultist standing on the leader's lane two turns earlier.

## 5. Ranked missing capabilities

| # | Capability | Evidence | Frequency | Est. impact | Cost | Smallest test |
|---|---|---|---|---|---|---|
| 1 | **Pressure on the enemy leader while neutrals remain** (zoning, chip damage, kill) | Strong association (80% vs 46%; 67% vs 8%), within-map 47:22, positions A/C. Causality unproven. | BOSS leader ≥9 HP at t=30 in 74% of 219 games | Large if causal | Root-only, zero nodes | §6 |
| 2 | **Access to contested neutrals** (leader centrality, cultist presence near them, denial) | Consistent in both seats and all versions; weak early predictor; position B. | Every game | Large, but every static form tried so far lost self-play | Evaluation term or plan-level search | BOSS-gated test of a non-leader-timid form, after #1 |
| 3 | **Tempo in tied positions** (shoot share 30.0 vs 35.7%, 2.2 waits/game, slow conversions) | Moderate; outcome link unmeasured | Most turns are ties (median gap < 25) | Unknown | Root tie-break | Folded into §6 manipulation check |
| 4 | Deeper tactics (6→8 ply reversals) | Real, 6–8% of decisions, **not** associated with losses | 0.5–0.6 per game | Small | 2.33× nodes for extension | None recommended |
| 5 | Larger / better root policy | No effect on choices | — | ~0 | High | None recommended |

Why earlier fixes failed:
- Race and leader-HP terms were symmetric evaluation terms. They also made our own leader chase or hide (`LHPW=40`: 40.1% self-play; race step 600: 33.5%).
- Gates were self-play, which did not transfer, or 4–16 BOSS games, which cannot detect less than about a 35-point change.
- Optimised targets (Legend-move imitation, nodes, self-play score) do not touch the decisions in §4.1: the engine already agrees with BOSS on 57% of moves and sees the rest as ties.

What would falsify each explanation:
- #1: raise early damage on BOSS's leader and score does not move → symptom, not lever.
- #2: equalise territory at t=20 and BOSS's conversions #4–#6 do not fall.
- Uniform-deficit reading of the seat split: 40+ unselected second-seat games scoring far below what the first-seat score predicts.

## 6. One next experiment: root leader-pressure tie-break, gated on BOSS 3

**Question.** Is early damage to BOSS's leader a lever or a symptom?

**Baseline.** 41711135, SHA-256 `902527dc4e76421fa7f6e20190d7a6d97a3ee8ef7e22e61deeb7caddb772b4e9` (`validation/nn-41710886/arena/41711135.c`).

**Isolated change.** Root only. While the enemy leader is alive and at least one neutral is alive, a root move whose shot victim is the enemy leader is compared with +EPS added to its search value (EPS = 50; compile switch, 0 = baseline). No evaluation change, no change below the root, no extra nodes, 49 ms unchanged. Because it is not an evaluation term it cannot make our own leader timid, which is what sank `LHPW`.

**Offline pre-check already done** (`tiebreak_probe.py`, `lhp_probe.py`, not strength evidence): on 137 of our real decisions (t 13–45) where a shot on BOSS's leader was legal, we took it in 25%; such a shot was within 50 points of the depth-6 best in 55%, within 100 in 66%.

**Before any game.** Switch off: identical move, value, nodes on the existing fixed-depth fixtures; `make test`, sanitizers. Self-play at 20 ms, 200 seat-paired games: sanity floor 45% only, not a gate.

**Cases.** First seat: 20 fresh seeds never used (e.g. 41714001–41714020). Second seat: every arena battle of 41711350 where BOSS 3 is first seat (unselected, unseen), topped up to 20 with the 16 known second-seat maps, reported separately as reused. Both arms on every case, order alternated, 2 runs each (BOSS is not deterministic: on identical histories it deviated first in 215 of 636 game pairs). 160 games, inside 200 per 6 hours. Verify agent 4611894 in every log.

**Metrics.**
1. Manipulation check, read first: share of games with BOSS leader ≤ 6 HP at t=30. Reference 19% (41/219).
2. Outcome: map-clustered paired score difference, by seat.
3. Guards: our leader HP at t=30, our conversions by t=40, max `elapsed_us`.

**Stop rules, fixed in advance.**
- Check 1 rises by less than 10 points → the change did not do its job. Stop; no conclusion about the hypothesis.
- Check 1 passes and score difference ≥ +10 points with second seat ≥ 25% → repeat once on 40 new cases before any submission.
- Check 1 passes and score difference ≤ 0 → leader pressure is a symptom. Drop #1, move to #2.

**Power.** 80 case pairs give a standard error near 7 points, so only effects of about 15 points or more are detectable. Treat any result as a pilot.

## 7. Established, hypotheses, unknowns

Established: §1 chronology and hashes; simulator and tracker exact on all boss games; first seat ≈ parity, second seat ≈ 15%; BOSS non-deterministic; BOSS not tactically superior at depth ≤ 6; horizon reversals and root NN are not what separates wins from losses; the behavioural differences in §4.1; the leader-HP association.

Hypotheses: leader pressure is causal (#1); access to contested neutrals is the upstream cause and leader damage is how we occasionally break it (#2); the second-seat collapse is the uniform deficit amplified by tempo rather than a separate defect.

Unknown: BOSS's algorithm, depth and evaluation; whether any of this transfers to the rest of Gold, which sets the rating that decides promotion; unselected second-seat performance beyond 10 games.

## 8. Reproduce

From `validation/boss3-diagnosis-20261114/`. Needs Python 3, a C compiler, Java 11+ (`model-j11.jar` is the unmodified referee model plus `arena-study/scripts/Batch.java`, compiled with `--release 11`).

```sh
python3 collect.py && python3 recon.py      # all_games.json, boss_games.json (225 games)
python3 check_eng.py                        # 31,120 transitions vs Java
python3 determinism.py; python3 logs.py
python3 behave.py; python3 territory.py; python3 neutrals.py; python3 swings.py; python3 wl.py
python3 assoc.py; python3 leaderhp.py; python3 leadershots.py; python3 exposure.py; python3 defer.py
python3 bosspos.py 6 46 1                   # resumable, 150 s slices; then: 46 78 2 mid ; 78 112 2 late
python3 sidepos.py 6 40 2 us-a us           # then: 40 76 2 us-b us ; 76 112 2 us-c us
python3 bosspos_an.py bosspos.jsonl; python3 cells.py; python3 blunders.py
python3 horizon.py 13 43 2 0 4              # K = 0..3; then horizon_an.py
python3 lhp_probe.py; python3 tiebreak_probe.py
python3 pivot.py 921374717 19,21,23,25,27,29
python3 show.py 921374717 17,27,37 14-40    # board + moves; also 921222750, 921367944
```

Limits: re-analysis uses fresh caches and history, so it is not judge-identical (in-game move = fresh 49 ms move in 81.5%). Depth 6/8 values share our evaluation and bounded quiescence. Replay maps repeat; second-seat replay maps came from lost games. The timed probes ran on a 4-core VM, sometimes with two jobs in parallel.
