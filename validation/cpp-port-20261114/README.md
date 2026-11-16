# C++17 port comparison

`../../bot.cpp` ports the current C engine to standard C++17: named structs,
constexpr engine constants, C++ aggregate initialization, explicit compact-state
narrowing. Search, evaluation, NN bytes, POSIX clock, protocol and default 48 ms
budget are preserved. `baseline.c` freezes the input C source. Source hashes and
compiler/platform information are in `manifest.json`.

## Results

64 local games on 32 real arena maps, each played with C++ in both seats:
**24 wins, 18 draws, 22 losses; 51.56% score** (draw = half point).
Approximate paired 95% score interval: **44.98–58.14%**. No demonstrated strength
gain. Raw final unit counts and game lengths: `match_cpp_0.jsonl`; summary:
`summary.json`. Both engines built with the same Apple Clang toolchain, `-O3
-march=native`, respective C11/C++17 standards and equal 48 ms budgets. Games ran
sequentially. Some early games overlapped local compilation/verification, so
wall-clock search decisions can reflect scheduling noise.

Fixed-depth comparison: 48 positions from 16 real maps, sampled at plies 0, 4,
8; depth six, three repetitions each, alternating C/C++ order, engine reset
before each search. Actions, scores, depths and node counts match exactly.
144 transition comparisons also match. Final benchmark ran after all matches:
sum of per-position median search times **C 339.305 ms; C++ 339.319 ms**,
**1.0000x speed ratio**. Runtime effectively identical; timings exclude process
startup, map initialization and stdin parsing. Raw measurements: `performance.json`.

Strict `-std=c++17 -pedantic-errors` standalone compilation passes. All four
existing engine suites pass against the C++ engine with ASan/UBSan; persistent
12-turn protocol check passes (final isolated max latency 50.5 ms). `tests.log`
records the combined test target. Legacy C test fixtures permit their original
narrowing/compound-literal syntax; the production C++ source requires no such
extensions.

Local matches use the existing in-process C simulator and known RNG state,
not an independent Java referee or full observation-tracker match. Tracker is
covered by the existing engine suite. Local results do not establish arena
rating or judge timing. Time-limited searches may pick different moves despite
fixed-depth parity. Nothing submitted remotely.

## Reproduce

Run from repository root:

```sh
make bot_cpp
make test-cpp
python3 validation/cpp-port-20261114/verify.py
python3 tests/arena_match.py \
  --a validation/cpp-port-20261114/baseline.c --b bot.cpp \
  --tag cpp --out validation/cpp-port-20261114 \
  --maps-used 32 --shuffle-seed 2024 --budget-ms 48
python3 tests/arena_match.py --summarize \
  --tag cpp --out validation/cpp-port-20261114
```

Matches resume existing JSONL results; use a fresh output directory for a new run.
