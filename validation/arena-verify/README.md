# Arena study verification — 2026-11-13

Supports `RESEARCH.md` §7. Re-runs `validation/arena-study/` from its saved archive with independent scripts, plus new experiments. Public data only; no token, submission or boss test.

Run from a scratch dir holding `scripts/*`, `bot.c`, and the Java model compiled with `arena-study/scripts/Batch.java` into `java/out`:

```sh
python3 recon.py ../arena-study/results/games-2026-11-13.jsonl.gz   # recon.txt
python3 v1.py      # counts, seats, style, phase decomposition with CIs -> results/recheck.txt
python3 ds.py      # feature dataset (X.npy, S.npy, W.npy, maps via L.py)
python3 t2.py      # log-loss over 8 game-level splits, phase scale, rating/seat controls
python3 sv.py <part> <nparts> <depth> 16000 ; python3 sv2.py   # search-value predictor
python3 ag.py bot_r.c:RACEK=180 tag_0 0 4 ; python3 agsum.py tag   # Legend agreement (BMS=500 for long think)
python3 mt.py bot.c bot_r.c:RACEW=600:SOLOW=200 <part> 4 400 25 tag ; python3 ms.py tag   # seat-paired match, resumable
```

`scripts/bot_r.c`: `bot.c` plus macros `RACEW`/`SOLOW` (step race), `RACEK` (smooth race), `HPW`, `LEADERB`, `LEADERLATE`; all defaults reproduce production. `L.py` paths assume the workspace is mounted at `$HOME/mnt/cultist-wars`; `mt.py` needs `maps.json` (game id, walls, initial state for each validated game).

Results: `recheck.txt`, `texel_recheck.txt`, `search_value.txt`, `agree_variants.txt`, `matches.txt`, raw `match_*.jsonl`, `agree_*.jsonl`, `search_d*_values.jsonl`.
