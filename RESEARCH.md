# Cultist Wars AI research

Revised 2026-11-13 (second pass; third-pass verification added as §7). Scope: live arena data (3,182 recent ranked games), top-player behavior, postmortems, papers, engine docs, current `bot.c`.
Previous version (first pass, same day) superseded; its search-side items are now implemented or tested (history, threat-aware Q, LMR). Raw evidence: `validation/arena-study/`.

**Bottom line: search is not the bottleneck. Evaluation is.** Wins/losses at the top are decided by the midgame neutral-recruitment race and endgame kills. Our static eval predicts held-out game results clearly worse than seven simple features fitted on top-player games, and our engine sees most Legend-vs-us move disagreements as near-ties. Next work: fit evaluation on arena data (Texel), add recruitment-race features, test at much larger volume.

**Third-pass correction (§7):** the numbers below reproduce, but two conclusions do not hold up. Our depth-6 search value predicts results as well as the fitted features, and adding the neutral-race term to the eval loses games (33.5% at the fitted weight). §7.6 replaces the §4 ranking.

## 1. Arena snapshot (live, 2026-11-13 ~19:00 UTC)

Public leaderboard, `getFilteredPuzzleLeaderboard`. 400 ranked: Wood 264 / Bronze 90 / Gold 30 / Legend 19 agents (league indices 0–3).

- **hexagonee: Gold #3, global #22, 24.91.** Above us in Gold: Lv0Sys 25.56, N3l 25.44. Gold boss last recorded 26.92 (`validation/lmr/cg-progress.json`); need to pass it for Legend.
- Legend top: aCat 33.92, Noobkins 30.60, MSz 30.11, Zylo 27.20, KawattaTaido 25.72, jacek 25.15, Pduhard- 23.89, putibuzu 20.04, tarriel 19.87 (Rust). Legend languages: C++ 16, Rust 2, C 1. Bottom of Legend: ziele 13.34.
- Activity is high: aCat, Noobkins, yhyoxx, ziele, tarriel all resubmitted within the last ~3 days (agent creation times). Expect moving targets.

## 2. Latest-match study (our data, not self-reports)

Method: `findLastBattlesByTestSessionHandle` for 12 players (9 Legend, us, Lv0Sys, N3l) → 3,182 unique finished games; full stdout via `gameResult/findByGameId`. Reconstructed with the **unmodified Java model** (batch adapter `Batch.java`, same logic as `validation/strength/Replay.java`). Final unit counts match reported scores in **3,097/3,182** games; all 85 mismatches are timeout/crash games (score −1). Per-player stats below use each player's *current* agent only. Samples = recent ranked games, not full histories.

### 2.1 Outcomes and style

| Player | Games | Score% | as P0 | as P1 | Shoot% | Move% | Convert% | Wait% | Neutral shots/g |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| aCat | 402 | 74.8 | 89.5 | 59.4 | 40.1 | 47.1 | 10.3 | 2.5 | 3.61 |
| Noobkins | 113 | 68.1 | 93.1 | 47.6 | 34.6 | 53.0 | 11.1 | 1.3 | 1.40 |
| MSz | 416 | 56.0 | 72.1 | 39.6 | 34.3 | 51.0 | 12.8 | 1.9 | 1.34 |
| Zylo | 437 | 41.9 | 55.5 | 28.1 | 32.0 | 49.0 | 10.6 | 8.4 | 3.12 |
| jacek | 342 | 35.7 | 48.9 | 21.7 | 30.5 | 54.2 | 11.3 | 3.9 | 3.23 |
| hexagonee (Gold pool) | 93 | 67.2 | 80.7 | 55.1 | 31.2 | 56.4 | 8.7 | 3.8 | 1.74 |
| Lv0Sys (Gold) | 222 | 42.1 | 51.3 | 32.1 | 24.9 | 61.2 | 10.1 | 3.8 | 0.10 |

Shares are of each player's own actions; the opponent pools differ by league, so cross-league score% is not comparable.

- **First-seat advantage is large.** All games: P0 wins 1,793, P1 wins 1,018, draws 371. Every player scores far better as P0. Local tests must stay seat-balanced; P1 play is where points are lost.
- **Top players shoot more.** aCat 40% shots vs our 31%; we move the leader more.
- Enemy conversions roughly cancel (ping-pong): per game, conversions gained ≈ conversions lost for every player.

### 2.2 Where games are decided

Net unit swing per game, from the player's side, by phase. N = neutral conversions, K = kills of non-neutral units (enemy conversions net ≈ 0, omitted).

| Group | n | N 0–40 | K 0–40 | N 40–100 | K 40–100 | N 100+ | K 100+ |
|---|---:|---:|---:|---:|---:|---:|---:|
| aCat wins | 282 | −0.08 | +0.23 | **+0.82** | +0.29 | +0.35 | **+1.03** |
| aCat losses | 83 | −0.59 | +0.06 | **−1.12** | −0.29 | −0.12 | −0.48 |
| MSz wins | 210 | +0.05 | +0.08 | **+0.97** | +0.58 | +0.35 | +0.88 |
| Noobkins wins | 70 | −0.14 | +0.01 | **+1.13** | +0.44 | +0.20 | **+1.16** |
| hexagonee losses | 18 | −0.06 | −0.11 | **−1.06** | +0.11 | −0.22 | −0.72 |
| hexagonee vs Gold boss | 8 | +0.00 | +0.25 | **−0.75** | +0.38 | +0.12 | −0.25 |

Same pattern for every player examined: **the midgame (actions 40–100) neutral-recruitment race and endgame kill exchanges decide results.** The opening is nearly even. When we lose, we lose the midgame recruitment race and then also lose endgame trades.

Against the Gold boss in the arena: **3W/0D/5L**; as P1 **0/3**, each time behind by action 40 and never recovering. Our other losses show the same "−1 by turn 30–60, stays negative" curve (`results/our_nonwins.json`).

### 2.3 Our engine vs Legend moves

Fed 1,900 sampled positions where aCat, Noobkins or MSz moved into current `bot.c` at 49 ms (exact RNG known, as in judge logs).

- Agreement with Legend moves: **aCat 43.4%, Noobkins 46.0%, MSz 42.4%.** Control: our own arena moves **87.0%** (timing/hardware noise, local vs submitted source).
- Main disagreement: they **shoot**, we **move** (aCat: 133/900 positions; 15%). We move the leader more often (274 vs 191 per 900).
- Depth-5 child scores for 1,067 disagreements: **median gap 6 points** (1 unit = 1,000); 73% < 50. Our engine sees most of their choices as ties: tie-breaks come from the eval.
- 16% of disagreements have gap > 500 (we think their move loses ≥ half a unit). In the real games they did lose material within 6 plies more often (35% vs 19% baseline), but were back to −0.04 units on average by 20 plies. They accept short-term material loss for longer-term gains our horizon/eval misses.

### 2.4 How well does our eval predict results?

Texel-style check (`scripts/texel.py`): 124,345 positions (every 3rd ply, decisive games) from the 3,097 validated games; train/test split by **whole game** (75/25). Logistic loss on held-out games, side-to-move perspective:

| Predictor | Test log-loss |
|---|---:|
| Our static `evaluate()` (scaled fit) | 0.499 |
| unit count only | 0.521 |
| + HP diff, leader diff | 0.487 |
| + **neutral race** (neutrals leader reaches first; mover wins ties) | **0.435** |
| + neutrals×leader, count×lateness, HP×lateness | **0.396** |
| all above + our eval | 0.378 |

Per phase (4 features vs our eval): opening 0.643 vs 0.690; midgame **0.385 vs 0.482**; endgame 0.188 vs 0.219.

What the fit says (correlational; collinear features; not causal):
- **Neutral race is the single biggest missing feature** (−0.052 log-loss).
- **Material matters more as the game ends** (count×lateness coefficient ~3 on top of our eval). Our eval values a unit the same at action 10 and at action 145.
- HP matters a lot in the midgame (per-HP coefficient ≈ 0.3× count coefficient). Ours: 18 per HP vs 1,000 per unit.
- With our eval present, leader-alive gets a strongly negative residual (≈ −520 eval points): our fixed **+420 leader bonus is probably too large**. Leaders cannot shoot; their value is recruitment, which the race feature carries.
- Caveat: we fit the static eval, not the Q/search value; better prediction ≠ stronger play.

### 2.5 Pilot matches (single-weight changes)

Equal-time 49 ms, real arena maps (Java-reconstructed initial states), 30 maps × both seats, current `bot.c` family:

| Change | W/D/L | Score | Legend agreement (700 pos.) |
|---|---:|---:|---:|
| baseline | — | — | 45.0% |
| HP weight 18 → 60 | 22/13/25 | 47.5% | 45.4% |
| leader bonus 420 → 0 | 26/13/21 | 54.2% | 44.9% |
| +600 per unit × lateness (cache key incl. remaining) | 22/16/22 | 50.0% | 44.9% |

**All inside noise** (60 games ≈ ±12 points at 95%). Neither self-play nor agreement moves. Conclusion: hand-nudging one weight will not close the gap. The fitted features must arrive together, with a proper race term, and be tested at far higher volume. Self-play inside one engine family also hides gains against different styles.

## 3. External evidence

### 3.1 Postmortems (2022 contest, [forum p.2](https://forum.codingame.com/t/community-puzzle-cultist-wars/98099?page=2), [p.3](https://forum.codingame.com/t/community-puzzle-cultist-wars/98099?page=3))

- **MSz, 1st, now Legend #3:** iterative-deepening alpha-beta, hash move, quiescence, aspiration, futility, depth/position pruning. Eval: material + precomputed BFS distances, deliberately simplified for depth. ~120k states/turn, pruned depth ~16. Exact neutrals/indirect shots; 128-bit masks for killable units.
- **KawattaTaido, 2nd, now Legend #5:** negamax alpha-beta + **NN eval** (364-32-32-32-1). Inputs: HP diff, unit diff, leader-to-nearest-neutral, **neutral distance distribution**, units per row/col. Depth 6–8, ~30k sims/turn; NN 50–70% of time; NN cached by Zobrist. Leader-distance distribution added so the leader stops chasing small groups. Recommends local arena + eval-vs-turn plots to find eval jumps.
- **jacek, 3rd, now Legend #6:** best-first minimax with UCT + NN (729→80→80→1), TD learning then Texel-like tuning on 4-ply minimax positions. History mattered. Referee calls RNG even for dead units.
- **OldJohn, 5th, now Legend #18:** alpha-beta, TT, killers; **linear eval fitted by logistic regression on ~40k self-play games**; 3 regimes (no neutrals / one leader + neutrals / neutrals with 0 or 2 leaders). Features: leader HP, cultist HP, neutral HP, flood distance to nearest neutral and average neutral distance per leader. **Reports Gold → Legend.** Tip: if the enemy leader is chasing your cultist, shoot your own cultist (lose 1, not a 2-swing).
- **TomAlard, 8th, now Gold #30:** local 12-thread arena; tuning gave +20% self-play; **"HP far more important than first assumed."** Dropping neutral shots from search limited strength.
- **VizGhar (heuristic, 30th):** conversions first, flood fill to nearest neutral; retreat via HP-vs-damage table.
- Referee quirks: target-dependent Bresenham (aCat's 2022 shot through a "blocked" line, step 118); lower-Y-first line; LCG `rand(MAX_NUMBER_OF_NEUTRALS)`. All already modeled exactly (17 games/2,207 transitions earlier + 3,097 games here).
- Forum after 2023: only Legend-league chatter (jacek, Aug 2025). No new postmortems found. aCat, Noobkins, Zylo, tarriel have published nothing on this game that we found.

### 3.2 Methods literature

- **Texel tuning** — logistic regression of eval terms on game results; hundreds of parameters at once; [chessprogramming.org](https://www.chessprogramming.org/Texel%27s_Tuning_Method), [Österlund's original thread](https://talkchess.com/viewtopic.php?p=551944). Known pitfall: ignores search/eval interaction. Fix K, let unit value float; interpolate phase terms before search sees them.
- **SPSA** — for search knobs and parameters that resist regression; [Spall overview](https://www.jhuapl.edu/SPSA/PDF-SPSA/Spall_An_Overview.PDF).
- **jacek's NN-input notes** — one-hot inputs, 1 hidden layer, tanh output, side-to-move flip; incremental updates on expansion; small nets for CG code-size; feature engineering still matters for spatial games. [Tech.io playground](https://tech.io/playgrounds/157341).
- **Best-first minimax with UCT** — jacek's search; beats plain minimax with a good eval, can be "blind" without one. [Playground](https://tech.io/playgrounds/55004).
- **Unbounded best-first minimax / Descent** — Cohen-Solal: value-only learning + best-first search; strong across board games; 2026 completeness proof. [2008.01188](https://arxiv.org/html/2008.01188v5), [2603.24572](https://arxiv.org/html/2603.24572v1).
- **MCTS–minimax hybrids** — minimax embedded in MCTS helps in tactical domains. [Baier & Winands](https://dke.maastrichtuniversity.nl/m.winands/documents/mcts-minimax_hybrids_final.pdf).
- **Stockfish NNUE** — quantized incremental eval; reference only. [docs](https://official-stockfish.github.io/docs/nnue-pytorch-wiki/docs/nnue.html).
- **Fishtest** — sequential tests (SPRT), fixed error bounds before looking. [FAQ](https://official-stockfish.github.io/docs/fishtest-wiki/Fishtest-FAQ.html).
- Local runners: [cg-brutaltester](https://github.com/dreignier/cg-brutaltester), [codingame-arena](https://github.com/mrsombre/codingame-arena) (we already have our own C harness + Java reconstruction).

## 4. Ranked roadmap to Legend

Ranked by expected gain per effort, weighted by evidence strength. "Evidence" = what supports the bet, not proof.

### #1 — Data-fitted evaluation (Texel on arena games) · highest odds

**What:** Replace hand-set eval weights with a logistic fit on top-player positions. Features: unit count, HP (per role; try linear + concave), **neutral race** (per neutral: leader arrival difference with tempo; weight by neutral HP), neutrals×leader alive, **phase scaling** (count/HP × lateness), enemy-cultist conversion threat, current leader-proximity term as a candidate feature. Fit on Q-search leaf values, not static eval. Split by game; hold out whole games and maps.

**Evidence:** fitted 7 features beat our eval 0.396 vs 0.499 held-out; midgame gap largest, which is exactly where games are decided (§2.2). OldJohn reached Legend from Gold with this method. TomAlard: tuning +20%, HP underrated. KawattaTaido's NN uses the same feature families.

**Cost/risk:** medium. Eval cache key must include `remaining` (phase) and maybe side (tempo); +24% eval cost seen in `eval-race.c`, so implement race cheaply (precomputed BFS + leader position only). Grow data first: ~400 battles per Legend player available now; refetch daily.

**Gate:** held-out log-loss ↓, Legend agreement ↑ (≥ +3 pts on 2k positions), then ≥ 400 seat-paired games at 49 ms on real maps (SPRT), then private boss seeds both seats.

### #2 — Midgame recruitment play (race-aware leader routing, denial)

**What:** Once #1 values the race, make search *see* it: occupancy-aware leader routes at root, ordering bonus for leader moves that win a contested neutral, neutral-denial shots (aCat: 3.6 neutral shots/game vs our 1.7), escort/blocking cultists for the leader. Extend search on race-critical leader moves.

**Evidence:** midgame N swing −1.06 in our losses, −0.75 vs boss; 0/3 as P1 vs boss, all behind by action 40. Earlier diagnosis already ranked recruitment sequencing #3. KawattaTaido: leader-distance *distribution* feature stopped poor routing.

**Cost/risk:** medium. Depends on #1; alone it repeats `eval-race.c` (inconclusive).

### #3 — Testing infrastructure at real volume

**What:** (a) Real arena maps: all 3,182 reconstructed initial states, not synthetic symmetric maps. (b) 1,000+ game SPRT per candidate; 4 cores locally ~2 h at 49 ms, or screen at 10–20 ms first. (c) Opponent diversity: older frozen versions plus a "Legend-imitation" policy (see #5). (d) Offline proxies: held-out log-loss + Legend-move agreement as fast filters.

**Evidence:** all four §2.5 pilots and the earlier experiments landed inside noise. Without volume, real +3–5% gains are invisible. P0/P1 asymmetry (P0 wins 64% of decisive games) makes seat-balancing mandatory.

**Cost/risk:** low. Enables everything else. Do in parallel with #1.

### #4 — Endgame conversion of material (actions 100–150)

**What:** Phase-scaled material (from #1); exact terminal-count search when remaining ≤ ~12 plies; avoid trades that keep the count but lose tempo near the horizon; HP-preserving retreats late.

**Evidence:** endgame K +1.03 for aCat wins, +1.16 Noobkins wins, −0.72 in our losses; count×lateness is the second-largest fitted gain.

**Cost/risk:** low–medium; terminal scoring already exact near action limit.

### #5 — Small learned eval (NN) or learned move ordering

**What:** After #1, a tiny NN (KawattaTaido-style 364-32-32-1 scale, or jacek-style one-hot board + features), trained on arena outcomes + search-value bootstrap; int8 inference, Zobrist cache. Cheaper variant: a policy prior trained to predict Legend moves (43% → ?) used only for move ordering.

**Evidence:** #2 and #6 in the 2022 contest used NN evals; jacek's notes show large gains from NN in many CG board games. Policy-from-Legend data is new and plentiful.

**Cost/risk:** high (training pipeline, code size, 50–70% time in eval per KawattaTaido). Only after #1 shows that eval quality moves results.

### #6 — Second-seat strategy

**What:** Detect seat; as P1 value tempo explicitly (race feature with tempo already partly covers it); check opening choices as P1 against the top players' P1 lines (aCat holds 59% as P1).

**Evidence:** P0 advantage huge; our arena P1 55% vs P0 81%; boss P1 0/3.

**Cost/risk:** low once #1 exists (side bit already in hash; add to eval key).

### #7 — Search changes · low priority

Best-first minimax/UCT (jacek), deeper pruning (MSz), TT buckets, staged generation. MSz-style depth without MSz-quality pruning failed (LMR not promoted). Our engine already reaches depth ~7.6 at 49 ms; disagreements are eval ties, not missed tactics. Revisit only after eval work.

### Not recommended now

- Repeating hand-made HP tables, single-weight nudges, or LMR — tested, noise.
- RNG scenario aggregation — all 983 boss-test turns had exactly one RNG candidate.
- MuZero/AlphaZero-scale learning — exact simulator exists; compute budget is wrong.

## 5. Bot status vs this research

| Area | In `bot.c` | Gap from evidence |
|---|---|---|
| Search | ID negamax, PVS, aspiration, 64k TT, bounded history, guarded futility | Adequate for now |
| Q | 4-ply tactical, threat-aware full replies, lazy eval | Adequate |
| Eval | 1000/unit + 18/HP + 420 leader + leader proximity sum + fighter proximity | No race, no phase, HP tiny, leader bonus likely too large |
| Simulation | Exact Java-validated (now 3,097 more games via Java only) | — |
| Testing | Synthetic maps, 16–32 pairs | Real maps, ~1,000 games, SPRT, Legend agreement |

## 6. Evidence limits

- Arena games: last-battle lists, mostly vs same-league opponents; players' recent submissions changed during collection. Legend vs Gold stats not directly comparable.
- Reconstruction uses the Java model, not the full referee; validated by final counts matching scores (3,097/3,182; mismatches = timeouts).
- Agreement uses 49 ms on a 2-core container, not the judge; control 87% bounds the noise.
- Logistic fits are correlational, use static eval not Q value, and pool all players' games; coefficients are collinear. They rank hypotheses, not prove gains.
- Pilots: 60 games each, single engine family; inconclusive by design.
- No new arena submission or boss test made in this research. No Elo predictions.

## 7. Verification pass (third pass, 2026-11-13 ~20:30 UTC)

Independent re-run from the saved game archive with new scripts (`validation/arena-verify/`), plus new experiments. Same `bot.c` (SHA `ce733367…`). No submission, no boss test, no live leaderboard refresh.

### 7.1 What reproduced

| Claim (§) | Re-check | Verdict |
|---|---|---|
| 3,097/3,182 games reconstruct; 85 mismatches all timeout/crash (§2) | 3,097 / 85, all 85 carry a timeout marker or −1 score | ✔ |
| P0 wins 1,793 / P1 1,018 / draws 371 (§2.1) | identical; 63.8% of decisive (64.4% without timeout games) | ✔ |
| Per-player score, seat split, style shares, neutral shots (§2.1) | identical to ±0.1 for current-agent rows | ✔ (see 7.2 a) |
| Phase decomposition (§2.2) | identical means; 95% CIs now attached (`results/recheck.txt`) | ✔ (see 7.2 b) |
| vs Gold boss 3W/5L, 0/3 as P1 (§2.2) | identical | ✔, n=8 |
| Legend agreement 43.4 / 46.0 / 42.4% (§2.3) | fresh 49 ms run: 44.1 / 46.4 / 42.0% | ✔ |
| Log-loss 0.499 vs 0.396 (§2.4) | 0.504 ± 0.010 vs 0.389 ± 0.012 over 8 game-level splits | ✔ number, ✘ reading (7.3) |
| Pilots 47.5 / 54.2 / 50.0% (§2.5) | recomputed from saved match files | ✔ |
| Forum postmortems (§3.1) | re-read pages 2–4 | ✔ (see 7.2 d) |

### 7.2 Corrections and caveats

a. §2.1 table mixes two populations: W/D/L in `summary.txt` line 1 counts every game where the *pseudo* appears (all agent versions, incl. as opponent: Noobkins 1,218, hexagonee 118); the table rows use the *current agent* only. Table rows are right; ignore the first block of `summary.txt`.
b. "hexagonee losses n=18" excludes the 5 boss losses. All 23 losses: midgame N −1.17 ± 0.37. Boss row (n=8): −0.75 ± 0.83, i.e. not distinguishable from zero on its own.
c. Our own bot repeats its own 49 ms move only ~92% of the time run-to-run (timing noise). The 87% "control" is consistent with that; agreement differences under ~2 points on 1,900 positions mean nothing.
d. OldJohn's Gold→Legend fit used **~40,000 self-play games of his own engine**, during the 2022 contest field. Not arena games of other players, not today's Legend. It supports "fit eval by regression", not "fit to arena data".
e. Seat edge is larger than stated once rating is controlled: at equal rating P0 wins **69%** of decisive arena games (logistic on per-game rating gap, n=2,415; 68.7% in the 566 games with |gap| < 1.5). In our local self-play P0 takes 57% of decisive games.

### 7.3 "Eval predicts poorly" is mostly a calibration artifact

- Letting our eval's scale vary with game progress (one extra parameter) drops its log-loss 0.504 → **0.457**. A scale that depends only on `remaining` cannot change which move the search picks (all leaves at one depth share it). So ~40% of the headline gap, and the whole "count × lateness" finding, is irrelevant to move choice.
- Using our **depth-6 search value** instead of static eval (16,000 sampled positions, phase scale allowed): **0.378**, equal to the 7 fitted static features (0.374). Endgame: search 0.114 beats the features 0.138. The fitted features do not out-predict what the engine already computes.
- What survives: the **neutral race adds information on top of depth-6 search** in opening and midgame (0.651 → 0.612; 0.371 → 0.342), nothing in the endgame. Holds with rating gap as a control (0.365 → 0.345).
- Residual sizes on top of search value, midgame, both leaders alive: ≈ +550–680 points per neutral our leader reaches first; ≈ +200 per remaining neutral when only we have a leader; ≈ +40–80 per HP.
- "Leader bonus +420 too large" (§2.4): only next to a race term, or late. Alone on top of search value the leader residual is ≈ −140 midgame (fine) and ≈ −470 after action 100 (too big late).

### 7.4 Putting the race term in the eval makes the bot worse

`scripts/bot_r.c` = `bot.c` + optional terms (defaults = production). 25 ms each side, real arena maps, both seats, fresh map shuffle. Candidate score vs current `bot.c`:

| Candidate | W/D/L | Score | Paired 95% CI |
|---|---:|---:|---:|
| Race, step ±600 per neutral (fitted size), solo 200 | 49/38/116 | **33.5%** | 27.5–39.4 |
| Race, smooth K=600 | 69/45/86 | 45.8% | 39.6–50.2 |
| Race, smooth K=180 (same form as old `eval-race.c`) | 75/52/74 | 50.2% | 45.0–55.5 |
| HP weight 18 → 60 | 72/52/75 | 49.2% | 43.8–54.7 |
| Same bot, 50 ms vs 25 ms | 119/60/88 | **55.8%** | 51.2–60.5 |

- Dose-response is negative: the more race weight, the worse. A feature that predicts results is not a feature worth chasing; the race lead looks like a *consequence* of good play. Untested guess at the mechanism: the leader walks into fire to "win" races.
- Legend agreement with race terms: 44.4% baseline → 45.5 (K=180), 44.7 (step 300), 43.5 (step 600). No movement.
- Limits: self-play against our own engine at 25 ms, ~200 games each. A term could still help against Legend styles; nothing here suggests it.

### 7.5 Search really is not the difference in move choice

- 900 Legend positions at **500 ms** (depth 9.2 vs 7.2): agreement **44.3% vs 44.3%**; 58 moves switched toward the Legend move, 58 away.
- But time is worth something in results: doubling 25 → 50 ms scores 55.8%. A 2× engine speedup is hard and buys about that much.

### 7.6 Revised ranking

1. **Volume testing first (was #3).** 200 seat-paired games at 25 ms take 3 minutes on this machine and give ±5.5 points; ~1,300 games give ±2 (enough to see +3 with 80% power), ~20 minutes at 25 ms, ~40 at 49 ms. Not 2 hours. Every later item goes through this gate, then private boss seeds in both seats.
2. **Tune the eval by play, not by arena log-loss (replaces #1).** SPSA or OldJohn-style regression on our own self-play games, with search-leaf values. ~4,000 games/hour here. Arena fits stay as a source of hypotheses only. Do not add the race term as a static bonus.
3. **Second-seat play (was #6).** 69% P0 edge at equal rating; we score 55% as P1 in Gold, 0/3 vs boss. Study aCat's P1 games (59%) for what differs in the first 40 actions.
4. **Why Legend shoots where we move (part of old #2).** Still the largest move-level difference (shoot share 35.7% vs our 28.2% on the same positions) and unexplained: not depth (7.5), not race (7.4), not HP weight. Next test: move-ordering/eval terms for threatened HP and shot lanes, or a policy prior learned from Legend moves (old #5), each through gate 1.
5. **Search speed.** Worth ~+6 points per 2×; only exact-result speedups.
6. **Endgame terms (was #4): dropped.** Search already predicts endgames better than the fitted features; the endgame kill deficit in losses is most likely a result of being behind.

Unchanged: no NN before a cheaper eval change shows a gain; no LMR, hand HP tables, or single-weight nudges.

## 8. Implementation pass (2026-11-13 ~21:30 UTC)

Acted on §7.6: built the volume gate first, then tuned by play. Full report:
`validation/tuning/README.md`.

- **Gate** (`tests/arena_match.py`): real arena maps, equal time, every map in
  both seats, resumable. Baseline-vs-baseline control **49.62%** (CI 46.71–52.54,
  133 pairs), so the runner and the paired statistic are sound.
- **Promoted: flat leader bonus 420 → 0.** Candidate score vs the previous
  source: **53.23%** at 20 ms (240 pairs, CI 50.85–55.61) and **51.94%** at
  49 ms on a disjoint map sample (296 pairs, CI 49.67–54.21); pooled 536 pairs
  **52.52%** (CI 50.88–54.16). Dose–response peaks at 0: −250 → 43.5%, 0 → 53.2%,
  200 → 51.3%, 420 → 50.0%. Honest size: **about +2 points**, not the +4 the
  first 148 pairs suggested.
- **Not promoted: late-only leader discount** (420 early, 0 after action 100):
  50.00% (120 pairs). So the §7.3 residual reading — "too big only late" — is
  wrong about where the error lives; the flat term is simply not worth its size
  at any phase.
- `bot.c` keeps the weight as `LEADER_BONUS` (default 0), the same ablation
  pattern as `Q_LAZY_EVAL`. `-DLEADER_BONUS=420` restores pre-tuning leaf values,
  which is how `tests/test_differential.py` still verifies exact model and search
  parity against submission 41708923 (720 positions, 21,708 transitions, 24
  fixed-depth results). `make test`, `make sanitize` pass. The shipped source was
  verified bit-identical to the variant that was measured (200 fixed-depth
  comparisons, 0 mismatches).
- **Submitted as 41710268** (leader bonus 0 + the previously unsubmitted lazy-Q
  optimization). Arena ranking was still in progress when this was written; the
  bar is boss 26.92 against our 24.91.
- **Private Gold-boss series, 10 paired seeds: a tie within noise** — tuned
  3W/3D/4L (45%) vs baseline 5W/1D/4L (55%), paired unit-margin −0.40 (CI −1.98
  to +1.18). As predicted, this sample size cannot resolve a 2-point change
  (~80 paired games would be needed). Every private boss game is seat 0, so the
  series is blind to our weak second seat.

This is the first eval change in the project to clear its own noise floor. It
does not close the 2.0-point gap to the Gold boss (24.91 vs 26.92) by itself.
