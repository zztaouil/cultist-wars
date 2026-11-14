# Arena study — 2026-11-13

Supports `RESEARCH.md` §2. Public CodinGame services only (no token, no submission, no IDE changes).

## Contents

- `results/games-2026-11-13.jsonl.gz` — 3,182 recent ranked games: id, referee seed, agents, scores, normalized first-line stdout per action (`TIMEOUT` marks a timeout).
- `results/lb.json` — leaderboard snapshot at collection.
- `results/summary.txt` — outcome/style stats, phase decomposition, neutral-shot stats.
- `results/texel.txt` — held-out log-loss of our static eval vs fitted features.
- `results/agree.jsonl` — 2,200 positions: Legend/own move vs our 49 ms move (includes state + walls).
- `results/gap.jsonl` — depth-5 child scores for 1,067 disagreements.
- `results/m_*.jsonl` — pilot equal-time matches (HP 60, leader 0, lateness 600), 30 maps × 2 seats.
- `results/rows.json`, `results/our_nonwins.json` — per-game features; our non-wins with event timelines.
- `scripts/` — `fetch.py` (download), `Batch.java` (batch Java-model reconstruction, put in `model` package), `recon.py`, `load.py`, analysis scripts, `harness.py` (ctypes wrapper for `bot.c`), `match.py` (equal-time matches on real maps), `bot_v.c` (pilot variant: `HPW`, `LEADERB`, `LATEK` macros; defaults = production behavior).

## Reproduce (container used Python 3.13, numpy, scikit-learn, JDK)

```sh
python3 scripts/fetch.py <players...>      # needs data/handles.json from leaderboard
javac -d java/out <model>/*.java scripts/Batch.java && python3 scripts/recon.py
python3 scripts/stats.py; python3 scripts/decomp.py; python3 scripts/texel.py
python3 scripts/agree.py 0 2 & python3 scripts/agree.py 1 2
python3 scripts/match.py bot_v.c bot_v.c:LEADERB=0 0 2 30
```

Scripts expect a `data/` working dir (games, recon.txt) and `bot.c` beside them; paths are relative. Validation: final counts match reported scores in 3,097/3,182 games; 85 mismatches are timeout/crash games.
