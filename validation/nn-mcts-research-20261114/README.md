# NN inside search: research assessment — 2026-11-14

**Recommendation: test compact policy/value PUCT; strength gain plausible, unproven.** Main opportunity: learned long-horizon evaluation. Main risks: CPU inference cost, tactical refutations, value-training quality. Research only; no new matches, training, or production changes.

## Literature read

- [Silver et al., AlphaZero preprint, 2017](https://arxiv.org/html/1712.01815): state network supplies action priors and expected outcome; search repeatedly evaluates new leaves, backs up values, improves policy. Self-play supplies search-policy targets and terminal outcomes. Establishes architecture, not feasibility under our 48 ms CPU budget.
- [Danihelka et al., Policy improvement by planning with Gumbel, ICLR 2022](https://openreview.net/pdf?id=bERaNdoegnO): limited simulations can leave root actions unvisited; sampling without replacement and sequential halving improve low-budget planning. Read [accessible review manuscript](https://openreview.net/references/pdf?id=HuXQn5wWBV); final PDF blocked by browser verification. Improvement guarantee assumes correctly evaluated action values; no guarantee for our imperfect NN. Relevant second variant after basic PUCT works.
- [Ramanujan, Sabharwal, Selman, ICAPS 2010](https://www.cs.cornell.edu/~raghu/Raghuram_Ramanujan_files/icaps10.pdf): UCT can miss shallow adversarial traps. Supports tactical regression tests. Studies UCT, not modern neural PUCT; does not prove neural MCTS unsuitable.
- [Wu, Accelerating Self-Play Learning in Go](https://arxiv.org/abs/1902.10565): abstract reports major training-efficiency improvements, still substantial compute. Context only; full methods not reviewed here. Avoid assuming AlphaZero-from-scratch is cheap.
- [DeepMind mctx](https://github.com/google-deepmind/mctx): official reference exposes priors/value at root and recurrent expansion, supports Gumbel search and training targets. Algorithm reference; JAX accelerator runtime not our submission architecture.

## Actual repository fit

Source inspected directly; historical README sections describe older networks.

- `bot.c`: 48 ms, 95,665 bytes at inspection. NN is 128→192→96→1, 43,393 parameters, grouped int8 storage decoded to float inference. `features(s,a)` and `nn_score` run separately for each root action. `nn_begin`/`nn_order` influence root ordering; negamax/quiescence retains handcrafted values. Scalar output is imitation logit, **not win probability or Q-value**.
- [Earlier tree-prior experiment](../nn-5m-20261114/README.md) added NN ordering at first opponent ply, not neural MCTS. Large model: 46.09% match score against root-only version, paired interval 40.09–52.09%. Archived median visits 520,656→407,296 (~21.8% reduction), mean depth 7.22→6.98. No demonstrated gain. Historical variants/maps, not fresh measurements of current source.
- [Loss audit](../arena-loss-audit-20261114/REPORT.md): 17/22 normal losses had fewer recruits; examples show delayed recruitment collapse and conversion loops. Suggests value/horizon weakness, not proof MCTS fixes it.
- Per-action dense NN costs ~43,264 multiply-accumulates, excluding features. At hypothetical 40 legal actions, ~1.73M MACs per expansion. Repeating current scorer throughout tree likely expensive; actual legal-action distribution and latency must be measured.
- Exact transition simulator already exists. Use it; learned MuZero dynamics adds unnecessary first-experiment scope. Preserve neutral RNG/member-list semantics, turn count, side, terminal scoring. Ambiguous RNG remains model uncertainty: retain baseline fallback initially, report that subset separately.

## Proposed search

At each simulation: select edges → reach unexpanded state → NN produces legal-action priors and value → expand → back up value. Cache outputs; do not rerun NN at every previously expanded node. No random full-game rollout required.

Use PUCT selection:

`argmax_a [Q(s,a) + c_puct * P(s,a) * sqrt(sum_b N(s,b)) / (1 + N(s,a))]`

Store Q from parent side-to-move perspective. Leaf value uses leaf side-to-move; flip sign across each player action during backup. Terminal result overrides NN: win +1, draw 0, loss -1. Initialize unvisited edges explicitly; tune first-play value on development positions. Choose largest root visit count for deterministic evaluation; exploration noise only for training. All legal actions retain access; no permanent top-k pruning.

Use shared state trunk plus cheap action-scoring head and value head. Encode walls, units/owners/HP/leaders, side, remaining actions; preserve unit identity/order where simulator semantics require it. Existing action-relative 128-feature network cannot become a shared state trunk merely by attaching value output. Retrain/distill. A small MLP is first candidate; parameter count selected by latency/quality, not largest source-fitting model.

First implementation: bounded node arena, per-turn tree, cached NN outputs, deadline checks around inference, legal fallback action. Reuse current simulator and exact terminal checks. Cache key must cover full search state; keep per-edge visit statistics separate from any shared NN cache. Defer graph backups and cross-turn reuse.

Optional tactical variant: bounded minimax/quiescence verification of threatened leaves/root finalists. Normalize its scores consistently with expected-outcome values. Existing alpha-beta cache bounds cannot be treated as exact NN values. Verify all evasions where threatened; heuristic tactical verification is not a general safety guarantee.

## Training and experiment gates

1. Freeze source, map split, seeds, opponent suite. Benchmark feature generation + inference + expansion, including initialization and high-branching positions. Record p50/p95/max latency, expansions, NN calls, root coverage, memory/source size. Estimate simulations from measured total cost, not MAC count alone.
2. Bootstrap policy from existing imitation corpus. Train value on audited complete-game outcomes, side-relative, splitting by canonical map/game before augmentation. Add stronger-search targets only after score calibration; retain outcome targets to reduce teacher bias. Include off-policy search leaves: played-position accuracy alone insufficient.
3. Compare: current baseline; PUCT with same priors + calibrated handcrafted leaf value; PUCT + learned value; alpha-beta + same learned value; optionally PUCT + tactical verification. Root-only policy control separates NN quality from search benefit. Gumbel sequential halving follows if measured simulation budget is small relative to branching.
4. Equal 48 ms total decision budget, same simulator/RNG treatment, both seats. Separate fixed-simulation diagnostics from wall-clock strength. Use development maps for tuning; freeze one finalist before fresh holdout. Suggested initial holdout: 256 independent maps × both seats (512 games), against frozen baseline plus targeted boss/opponent checks. No precision guaranteed by this count; inspect paired uncertainty.
5. Promotion requires paired 95% interval above 50% against baseline, no material tactical regression, legal outputs, source below 100,000 bytes, successful judge timing checks. Cluster intervals by map pair. No repeated holdout tuning; fresh confirmation if selection uses results. A draw-heavy inconclusive interval means insufficient evidence.
6. Only after initial value/search signal: iterative self-play, train policy toward search targets and value toward outcomes. Avoid expensive from-scratch training before inference and strength gates.

**Decision:** worth isolated prototype. Highest-value comparison: neural PUCT versus alpha-beta using the same learned value. If both improve similarly, value learning earned gain; MCTS replacement remains unsupported. If NN throughput collapses, shrink/shared-encode before spending more training compute.
