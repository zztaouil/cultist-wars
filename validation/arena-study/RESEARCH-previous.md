# Cultist Wars AI research

Researched 2026-11-13. Scope: current `bot.c`, supplied Java referee, game postmortems, chess engines, search papers, pathfinding tutorials, learning, experimental validation.

**Recommendation: retain fast alpha-beta. First experiments: history ordering, threat-aware quiescence, tuned evaluation.** Proposed benefits below = hypotheses; no bot changes or strength measurements made during research.

## Game-specific constraints

Local authority: `source/config/statement_en.html`, `source/src/main/java/com/codingame/game/Referee.java`, `source/src/main/java/model/Random.java`.

- 13×7 board; 14 initial units; one action per player turn; 50 ms response limit; 150 total actions.
- Win: eliminate opponent or lead surviving-unit count at limit. Leader death alone does not end game.
- Neutral conversion: count differential +1. Enemy conversion: +2. Enemy kill: +1. Health, positioning, recruitment potential still affect future outcomes.
- Shooting: Manhattan damage `7-distance`, range 6, exact referee Bresenham traversal; first blocker receives shot. Intended target and victim can differ.
- Neutral phase follows each action. Referee LCG starts at 42; draw consumption depends on neutral-list membership, selected entry, legal movement. Hidden RNG state matters even when board looks identical.
- Only one unit acts each turn. Extra units increase options and surviving count; they do not grant simultaneous damage or movement.

Consequences: avoid importing king-safety, piece-value, attack-map, null-move assumptions unchanged. Distinguish legal `WAIT` (neutral phase + turn consumption) from static stand-pat (no transition).

## Direct game evidence

Primary source: [Cultist Wars competitors' postmortems](https://forum.codingame.com/t/community-puzzle-cultist-wars/98099?page=3), September 2022. Historical contest placements, not current multiplayer ranks.

- **MSz, 1st:** efficient iterative-deepening alpha-beta; hash ordering, quiescence, aspiration, futility, selective pruning; cheap material/BFS evaluation; exact neutrals/indirect shots. Reported ~120k states/turn, average pruned depth ~16. Counting conventions unknown; not directly comparable with our visits.
- **jacek, 3rd:** best-first minimax with UCT, neural eval; TD pretraining, then shallow-minimax training. Reported network 729→80→80→1. History ordering mattered.
- **OldJohn, 5th:** alpha-beta, TT, killers; linear eval fitted from ~40k self-play games. Separate neutral/leader regimes; HP tables, recruitment distances. Reported regime-boundary artifacts.
- **TomAlard, 8th:** local arena, HP tuning, precomputed 128-bit shot masks. Omitting neutral shots reduced branching but limited play.

Separate primary evidence: [game creator's rule updates](https://forum.codingame.com/t/community-puzzle-cultist-wars/98099). Creator documents lower-Y bullet traversal and deterministic LCG neutral movement; selection uses fixed maximum neutral count.

## Current bot: already implemented vs gaps

Inspected `bot.c`; existing changes left intact.

| Area | Present | Research opportunity |
|---|---|---|
| Search | Negamax alpha-beta, iterative deepening, PVS, aspiration | Better ordering before stronger selectivity |
| Ordering | Hash move, static conversion/shot scores | Quiet moves otherwise equal; history, killers, countermoves |
| TT | 65,536 direct-mapped entries, full key | Depth/age replacement, small associative buckets |
| Leaves | Four-ply tactical quiescence, stand-pat | Escape/blocking moves absent; quiet threats can cross horizon |
| Evaluation | Unit count, linear HP, fixed leader bonus, BFS proximity | HP thresholds, recruitment races, phase/turn dependence |
| Simulation | Exact known-RNG neutral phase; observation tracking | Ambiguous RNG fallback holds neutrals stationary |
| Throughput | Packed units, occupancy masks, ray lookup, incremental keys, eval cache | Profile remaining cost before more representation changes |
| Validation | Differential/protocol tests, local paired arena, judge logs | Larger diverse holdout sets; tactical replay corpus |

Current throughput evidence: `validation/throughput/README.md`. Existing small match samples do not establish arena strength. More depth or more visits alone does not establish better play.

## Chess engines: what transfers

**Stockfish: best practical implementation reference for this bot.** Official site lists Stockfish 19, released 2026-09-05, and describes optimized alpha-beta plus NNUE. This is not a verified claim that one engine wins every hardware/time-control ranking. [Official site](https://stockfishchess.org/).

**Lc0: alternate architecture reference.** Neural evaluation combined with MCTS; useful for policy/value training and guided exploration. [Developer overview](https://lczero.org/dev/overview/).

**AlphaZero: research reference.** Self-play learns policy/value, guiding tree search. Historical chess results do not imply replacing our CPU-limited engine with untrained MCTS improves Cultist Wars. [Original paper](https://doi.org/10.1126/science.aar6404).

### 1. History, killers, countermoves — first search experiment

History promotes actions previously causing beta cutoffs. Original research evaluates interaction with other alpha-beta improvements. [Schaeffer, *The History Heuristic and Alpha-Beta Search Enhancements in Practice*](https://webdocs.cs.ualberta.ca/~jonathan/publications/ai_publications/pami.pdf).

Proposed adaptation:

- Start with quiet-move history keyed by side, leader/cultist role, origin, destination. Compare ID-sensitive variant separately; ownership can change.
- Reward cutoff move; penalize earlier searched quiet failures; bound/decay scores.
- Add two legal quiet killer moves per ply only if incremental gains survive testing.
- Countermove: prioritize successful response to previous action. Memory/cost may exceed benefit; test after basic history.
- Keep TT hint first. Compare conversion/lethal-shot priority against tactical scores mixed with history; fixed hierarchy is hypothesis.
- Preserve full-width legal action set in this experiment. Ordering-only fixed-depth result should match baseline with selective pruning disabled and compatible leaf semantics.

Primary implementation: [Stockfish move picker](https://raw.githubusercontent.com/official-stockfish/Stockfish/master/src/movepick.cpp) uses staged selection, capture/quiet histories, partial sorting. Teaching source: [University of Alberta alpha-beta tutorial](https://webdocs.cs.ualberta.ca/~mmueller/courses/2014-AAAI-games-tutorial/slides/AAAI-14-Tutorial-Games-3-AlphaBeta.pdf).

### 2. Threat-aware quiescence — first tactical experiment

Chess engines handle threatened kings specially: quiescence includes evasions; stand-pat restricted. [Arasan author's guide](https://arasanchess.org/programr.shtml). Stockfish also separates checked positions from static stand-pat. [Search source](https://raw.githubusercontent.com/official-stockfish/Stockfish/master/src/search.cpp).

Cultist Wars adaptation = **material-threat handling**, not literal check:

- Detect units killable by one legal opponent shot and cultists vulnerable to adjacent enemy conversion.
- Consider bounded quiet escapes, line-blocking moves, removal/conversion of attacker, recruitment moves creating immediate material threat.
- Several attacks on different units can overload opponent's single action; threat count alone cannot prove forced loss.
- Avoid stand-pat cutoff in selected unstable positions, or require short real-move verification. Thresholds need tuning.
- Compare against current four-ply leaves and deeper full-width reference on hand-picked positions.
- Do not declare all threatened units compulsory saves: sacrificing one may win elsewhere.

This targets possible horizon errors; current source alone does not prove a specific losing decision. Avoid adding expensive all-board exposure scans at every leaf: prior repo work removed them for throughput.

### 3. LMR — after ordering improves

Search late low-priority moves at reduced depth; re-search at normal depth when reduced score exceeds alpha. Author-level implementation reference: [Stockfish search](https://raw.githubusercontent.com/official-stockfish/Stockfish/master/src/search.cpp).

Conservative pilot: depth≥3; late quiet cultist moves; non-PV nodes; reduction one ply. Exclude TT move, leaders initially, conversions, shots, immediate escapes, newly created material threats, final-turn horizon. Full-depth re-search on promising reduced result.

LMR remains selective: re-search does not recover false negatives from reduced search. Threat detector and ordering quality determine risk. Log reductions/re-searches, shallow tactical disagreements, equal-time match results.

### 4. TT replacement and staged generation

Current search overwrites direct-mapped slot. Try depth-preferred replacement, then small buckets with age/depth policy at equal memory budget. Preserve full-state semantics: ownership, HP, positions, side, remaining turns, RNG state/knowledge, neutral list. [Stockfish TT implementation](https://raw.githubusercontent.com/official-stockfish/Stockfish/master/src/tt.cpp).

Try legal TT move before generating/sorting remaining moves. Generate tactical/quiet stages lazily if cutoffs justify overhead. Existing selection-sort loop can cost quadratic comparisons when many moves searched; profile against partial insertion sort/buckets. Avoid allocating all child states before knowing whether needed.

### 5. Extensions, null moves, pruning

Selective extensions: conversion races, forced lethal exchanges, last-unit threats. Bound extensions to prevent runaway trees.

Null-move pruning: defer. Chess assumption that passing usually worsens position transfers poorly. Legal `WAIT` still moves neutral RNG/turn horizon; artificial null move does not. Remaining turns make time itself strategic.

Futility/delta/SEE pruning: require game-specific swing bounds. Enemy conversion swings two units; blocker removal changes shots; neutral damage can deny recruitment or help opponent kill. Chess capture exchange evaluator cannot directly model these.

## Evaluation: learn small, cheap features first

Proposed feature set; all game-specific suggestions = deductions from local rules:

- Surviving unit differential, dominant near terminal horizon.
- Separate cultist/leader HP lookup tables. Damage thresholds make 1→2 HP and 9→10 HP strategically unequal; compare against linear and square-root HP.
- Leader recruitment value conditioned on reachable neutrals, enemy cultists, opponent leader, turns left. Fixed bonus persists even when recruitment impossible.
- Recruitment race: shortest leader route to a legal conversion-adjacent cell; add conversion action, side-to-move parity. Static occupied-target distance can misrepresent access.
- Best immediate shot, kill vulnerability, conversion vulnerability; compute sparsely or near root before paying per-leaf cost.
- Escape mobility, useful firing positions, ability to block/unblock important rays.
- Neutral denial: unrecruitable neutral may still block fire or control route. Never globally remove neutral shots.
- Remaining-action phase; avoid abrupt eval regime jumps. Leader value need not monotonically vanish: enemy conversion can remain available after neutrals disappear.

If eval becomes side-to-move/remaining-turn/RNG-dependent, **expand current board-only eval-cache key or split cached terms**. Current cache intentionally omits those fields.

Two training routes:

1. **Outcome-fit linear evaluation:** minimize error between `sigmoid(k * E(features))` and game result (win=1, draw=0.5, loss=0). Start with few feature groups. Split by complete game/map seed; positions from same game stay together. Balance phases; deduplicate; prevent seat bias. [Texel author's engine](https://github.com/peterosterlund2/texel) provides implementation context; direct game evidence above supports this approach.
2. **SPSA:** perturb parameter vector in two opposite directions; estimate noisy gradient from paired matches. Tune search margins/ordering constants or eval parameters; reserve independent final evaluation. [Spall's original overview](https://www.jhuapl.edu/SPSA/PDF-SPSA/Spall_An_Overview.PDF).

Do not treat lower prediction loss as proof of stronger play. Search/eval interactions need equal-time matches.

## Neural and alternate-search track

**Tiny NNUE-style eval:** sparse board features; incremental accumulator updates; quantized integer inference. Move changes few features; conversion toggles ownership; shot changes HP; neutral movement changes position. Include walls/map context and remaining turns. Benchmark total search strength, not only inference speed. [Stockfish NNUE technical guide](https://official-stockfish.github.io/docs/nnue-pytorch-wiki/docs/nnue.html).

Training teacher can be slower offline alpha-beta. Mix outcome labels and teacher values; validate on unseen maps and diverse opponents. Chess weights cannot evaluate this game. Stockfish's large training infrastructure is reference, not minimum hardware requirement for our small model. [Training documentation](https://official-stockfish.github.io/docs/nnue-pytorch-wiki/docs/training.html).

**Learned ordering:** predict promising actions, retain alpha-beta. Potentially cheaper migration than replacing evaluation/search together; compare ranking benefit against inference cost.

**UCT/MCTS:** alternate experimental baseline, especially with strong learned leaves. Original UCT uses bandit exploration in planning. [Kocsis and Szepesvári paper](https://aima.eecs.berkeley.edu/~russell/classes/cs294/s11/readings/Kocsis%2BSzepesvari%3A2006.pdf). Random playouts may underweight rare forced replies; test tactical corpus.

**PUCT:** learned policy priors direct exploration; value estimates replace full random rollouts. [Lc0 AlphaZero primer](https://lczero.org/dev/lc0/search/alphazero/). Best-first minimax, UCT, PUCT are distinct variants; test one at a time.

**Action abstraction:** scripted recruitment, retreat, focus-fire policies can propose moves or produce cheap playouts. Use proposals for ordering before hard omission. RTS portfolio methods motivate approach, but simultaneous multiunit assumptions differ here. [Churchill and Buro, *Portfolio Greedy Search and Simulation for Large-Scale Combat in StarCraft*](https://skatgame.net/mburo/ps/combat13.pdf).

**MuZero:** low priority. We already have exact referee dynamics; learning replacement simulator adds avoidable work. [DeepMind's model-learning paper](https://www.nature.com/articles/s41586-020-03051-4).

## Pathfinding, territory, action economy

Uniform-cost 91-cell map → precomputed BFS appropriate; A* every leaf unlikely useful. Occupancy-aware searches remain useful near root for blocked recruitment routes. [Red Blob's graph-search tutorial](https://www.redblobgames.com/pathfinding/a-star/introduction.html).

Multi-source distance fields can identify recruitment fronts, nearest threats, disconnected regions. [Red Blob's multi-source BFS tutorial](https://www.redblobgames.com/pathfinding/distance-to-any/).

Proposed root features: recruitment arrival margin; approach squares with cover; bottlenecks; route options. Validate real bullet geometry—Manhattan distance alone cannot establish danger.

Focus fire calculation: one best legal shot per player turn. Example: two available 6-damage shooters cannot instantly deal 12; second shot occurs after enemy reply. Likewise, leader moving three squares consumes three of own actions while fighters stay still. Value travel by opportunity cost, not just distance.

## Hidden RNG and endgame horizon

Known state: deterministic search already appropriate. Multiple consistent RNG candidates: uncertainty over hidden state, not fresh independent random direction every turn.

Experimental fallback: search consistent candidate scenarios; combine root values using explicit prior or conservative aggregation. Candidate tracking has no calibrated probabilities: uniform weighting is assumption. Never combine separate candidate searches as exact belief-state solution; future actions must depend only on observations actually available.

Stationary-neutral fallback remains cheap baseline. Compare inference coverage, root-move stability, runtime, strength. Expectiminimax is conceptual reference if genuinely stochastic transitions introduced; deterministic LCG with ambiguous state needs correlated trajectories.

Endgame: search through actual `remaining` horizon when feasible. Do not label repeated board as draw: RNG and remaining turns change future. Universal tablebase grows with walls, positions, HP, ownership, horizon, RNG. Root-specific exact few-unit solving more plausible pilot; retain ordinary search fallback.

## Validation roadmap

Primary testing reference: [Fishtest FAQ](https://official-stockfish.github.io/docs/fishtest-wiki/Fishtest-FAQ.html), [creating a test](https://official-stockfish.github.io/docs/fishtest-wiki/Creating-my-first-test.html). Sequential testing can stop once evidence sufficient; thresholds/error rates must be defined before inspecting results. Chess rating calibration does not automatically transfer.

1. Freeze baseline source/hash. Preserve current uncommitted work.
2. Build tactical positions from saved top-player replays: conversion races, low-HP escapes, blocker shots, neutral kills, leader sacrifices, count-preserving endgames, RNG ambiguity.
3. Ordering-only history experiment; fixed-depth correctness with pruning disabled, then equal-time paired matches.
4. Independently test bounded quiet evasions in quiescence. Measure tactical gains and lost depth.
5. Tune HP/recruitment/phase weights offline; independent holdout match set.
6. Test TT policy and LMR separately. Combine only validated patches; retest combination.
7. Only then compare tiny neural eval or best-first search.

Use same map seeds with seats swapped; include older bot versions and distinct styles. Hold seeds out of tuning. Keep fixed-node/fixed-depth comparisons for diagnosis and equal-time comparisons for playing strength. Record W/D/L, paired score uncertainty, timeout rate, completed depth, visits, first-move cutoff rate, TT cutoffs, quiescence cost, aspiration re-searches, RNG inference status.

Saved replays are ten sampled games, not complete player histories. Opponent stderr may be absent. Replay moves provide behavior examples, not proof of internal algorithm. Existing local simulator shares logic with bot; independent Java-referee checks remain valuable for consequential simulation changes.

## Wikipedia concept reading map

Requested concept references; technical recommendations above rely on primary papers, engine sources, tutorial authors and local referee. These links provide optional vocabulary/background, not independent validation of proposed changes.

- [Alpha–beta pruning](https://en.wikipedia.org/wiki/Alpha%E2%80%93beta_pruning)
- [Principal variation search](https://en.wikipedia.org/wiki/Principal_variation_search)
- [Transposition table](https://en.wikipedia.org/wiki/Transposition_table)
- [Quiescence search](https://en.wikipedia.org/wiki/Quiescence_search)
- [Zugzwang](https://en.wikipedia.org/wiki/Zugzwang)
- [Monte Carlo tree search](https://en.wikipedia.org/wiki/Monte_Carlo_tree_search)
- [Expectiminimax](https://en.wikipedia.org/wiki/Expectiminimax)
- [Efficiently updatable neural network](https://en.wikipedia.org/wiki/Efficiently_updatable_neural_network)
- [Linear congruential generator](https://en.wikipedia.org/wiki/Linear_congruential_generator)
- [Bresenham's line algorithm](https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm)
- [Biconnected components / articulation points](https://en.wikipedia.org/wiki/Articulation_point)

Additional navigation, cross-check against primary implementations: [Chess Programming Wiki history heuristic](https://chessprogramming.org/History_Heuristic), [late move reductions](https://chessprogramming.org/Late_Move_Reductions).

## Evidence limits

Web sources inspected, not an exhaustive literature review. Historical postmortem parameters/performance are self-reported. Stockfish master links mutable; pin revision before implementation comparison. No latest universal chess-engine ranking established. No predicted Elo gains. No strategy guarantees from static features. No bot edits, submissions, or new matches in this research task.
