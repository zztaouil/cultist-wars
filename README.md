# Cultist Wars C bot

Standalone C11 bot for the supplied Java referee. Submit `bot.c` as the C solution.

```sh
make
./bot < game_input.txt
make test
make sanitize
```

## Decision model

The bot simulates one legal player action per ply and evaluates the resulting
advantage. Negamax implements alternating-player minimax with alpha-beta pruning,
iterative deepening, tactical move ordering, previous-best ordering, and a 65,536
entry transposition table storing exact/lower/upper bounds. Completed iterations
supply the output; interrupted iterations never replace the last result. A legal
WAIT remains available if no iteration completes. No beam pruning discards moves.
Equivalent shots hitting the same victim are deduplicated.

Evaluation balances unit count, HP, recruitment potential, obstacle-aware distance,
leader survival, mobility, immediate firepower and exposure to enemy conversion.
These are hand-set heuristics, not parameters trained by a population-based
metaheuristic. Terminal evaluation follows the referee: leader death alone does
not lose; elimination or the 150-action limit decides by surviving unit count.

Bresenham paths are precomputed with the Java referee's strict tie comparisons
and lower-Y traversal (including its horizontal-direction convention). Shots hit
the first obstacle or living unit, including friendlies and neutrals. Damage is
7 minus Manhattan distance to the actual victim. Movement commands always name
an adjacent empty cell, avoiding dependence on the referee's distant-target
pathfinding fallback.

## Budget and approximations

Search uses a monotonic 35 ms deadline checked every 64 nodes, leaving headroom
inside the game's 50 ms response limit. Override with, for example:

```sh
make clean
make CFLAGS='-O3 -std=c11 -Wall -Wextra -Wpedantic -DBUDGET_MS=30'
```

Neutral movement is stochastic and its RNG state is unavailable in the protocol.
Search holds neutrals stationary; every real turn rebuilds the state from input.
Consequently search is exact for this deterministic approximation, not the full
stochastic game. Distance heuristics account for walls but ignore unit occupancy;
actual movement legality always checks occupancy. Weights and playing strength
have not been benchmarked against ranked opponents.

Diagnostics go to stderr: completed depth, advantage, nodes, cutoffs and cache hits.
The local protocol smoke test measured approximately 36.5 ms per turn on warm
runs. One initial cold executable launch measured 215 ms; process startup and host
scheduling are not covered by the internal search budget. Judge timing must be
validated separately.

## Verification

`make test` checks every distinct board endpoint pair against a direct port of the
referee's bullet traversal, with every possible intermediate unit/wall blocker.
It also checks conversion, friendly interception, damage, dead-unit removal,
terminal scoring, winning recruitment, and depth-three alpha-beta results against
unpruned minimax on twelve positions. The protocol smoke test checks legal output
across twelve input turns. `make sanitize` runs C tests under AddressSanitizer and
UndefinedBehaviorSanitizer.

The existing `source/` Java reference implementation is unchanged.

## Version control

After cloning, enable the tracked commit hook:

```sh
git config core.hooksPath .githooks
```

Commit messages must follow Conventional Commits:
`type(scope): imperative summary`. Scope is optional; use `!` before the colon
for breaking changes. Allowed types: `feat`, `fix`, `refactor`, `perf`, `docs`,
`test`, `chore`, `build`, `ci`, `style`, `revert`.

Start the summary with a lowercase letter or digit. Aim for 50 characters;
the header limit is 72 characters. No trailing period or whitespace. Separate
any body with a blank line. Explain breaking changes in the body.
Example: `fix(search): preserve completed iteration`.

The local hook rejects invalid messages. GitHub Actions validates commit history
on pushes and pull requests. Keep merge messages compliant or use squash merges
with a compliant title. GitHub branch protection must require the `validate`
check to enforce this policy before merging.
