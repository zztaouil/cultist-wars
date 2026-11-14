# Recruitment race / Gold-boss investigation — 2026-11-13 (evening)

Goal set by the user mid-session: **beat the Gold boss and reach Legend**. Promotion mode is
`rank_above_boss`, so the target is the arena rating, not only head-to-head boss games.
Stopped by the user before any candidate was promoted. `bot.c` is unchanged; nothing submitted.

## 1. Arena status

| Submission | Change | Final score | Gold rank |
|---|---|---:|---:|
| 41709358 | (previous) | 24.91 | #3 |
| 41710268 | leader bonus 420 → 0 | **23.85** | #4 |
| Gold boss (BOSS 3, agent 4611894) | — | **27.11** | #1 |

Gap to the boss: **3.3 points**. The leader-bonus change scored +2 points in self-play
(`validation/tuning/`) but the arena score fell 1.06. Arena reruns of the same code drift by
roughly half a point, so this is a strong hint, not proof, that **self-play does not predict
the arena** for this change.

41710268 arena games (82 finished, `arena-41710268/`): 43W/10D/29L. vs boss 1W/0D/5L.
One timeout in the sample, from the opponent (DomiKo), none from us.

## 2. Diagnosis

Data: the 3,182-game arena archive (reconstructed again with the Java model, all 3,182 OK),
plus the 82 new games. Scripts used for the measurements live in the session workspace; the
fetched data and boss logs are here.

1. **Leader routing is not the problem.** Our leader's moves go toward neutrals about as
   often as Legend leaders' (9.2 of 11 moves closer vs aCat 8.9 of 11).
2. **Leader damage tracks losses** for every top player (midgame: ~1.1 HP in wins, 2.3–3.0 in
   losses). Our leader takes 2.9 HP before action 40 and 3.6 in the midgame (Legend ~1.7),
   but Gold opponents' leaders take as much, so part of this is the Gold pool's style.
   Post-move exposure to enemy fire is only slightly higher than Legend's (0.50 vs ~0.41
   damage-in-line in the opening).
3. **Leader hunting dropped with the new build.** Hits on the enemy leader per own turn
   while neutrals remain (actions 20–110): old build 0.133, new build **0.078**
   (wins 0.100, losses 0.054). Legend: 0.05–0.08.
4. **Against the boss the deficit starts in the opening.** Replay logs (our stderr): in seat 1
   we are already ≈ −1 unit by action 21–31 in 4 of 6 games.
5. **Tempo, seen in traces.** Boss game 921315259 (seat 1): after action 19 our leader does
   not move for 25 turns while a neutral is 4 steps away (closer to us than to the boss);
   our cultists shoot neutrals and trade shots; the boss leader converts three more. Game
   921318921: the boss leader converts 4 midgame neutrals at 7 HP and no cultist of ours
   targets it. In our eval one leader step toward a neutral is worth ~17 points
   (1 unit = 1,000), so almost any other move outranks it.

## 3. Tools built

- `bot_x.c`: `bot.c` + macros, all defaults = production (verified to compile; equivalence
  test not run):
  `LHPW` extra per leader HP · `LPROXN` / `LPROXE` leader proximity % for neutrals / enemy
  cultists · `LNEAR` % on the nearest-target bonus · `LNEUT` leader value per remaining
  neutral · `FLEAD` cultist proximity to enemy leader while neutrals remain ·
  `LEXPW` / `LEXPK` leader exposure penalty.
- `bosstest.py` + `cgapi.py`: replays arena battles through the API with `replay_game_id`
  (same map, same opponent, **same seat**: the only way to test seat 1 against the boss).
  Resumable, one JSON per game in `boss/`; `--summary TAG`. Token read from a file, never
  printed.
- `gate_games.json`: 101 replayable games (93 battles of 41710268 + 8 boss games of
  41709358). Replays are only valid while those opponents stay testable (current league,
  last 500 battles of the current submission).
- `fetch_new.py`: public game-record pull without a token.
- `sweep.sh`, `chunk.sh`: self-play runners around `tests/arena_match.py`. Background jobs on
  the Mac VM die when each shell call ends; use short resumable chunks.

Real test-game limits (`GET /limits?puzzle=cultist-wars`): **10/min, 20/5 min, 30/15 min,
80/h, 150/3 h, 200/6 h, 600/24 h**. A 101-game gate per candidate does not fit; 14 boss
replays per candidate does.

## 4. Results

Boss replays, 14 arena boss games:

| Source | W/D/L | Score | Mean margin | Seat 1 |
|---|---:|---:|---:|---:|
| `bot.c` (41710268) | 3/1/10 | 25.0% | −1.71 | 0/6 won |
| `LNEUT=120` (13 games) | 4/0/9 | 30.8% | −1.77 | 0/6 won |

Self-play screen, 20 ms, real maps, seat-paired, vs `bot.c`:

| Candidate | Pairs | Score | Paired 95% CI |
|---|---:|---:|---:|
| `LPROXE=50` | 150 | 51.5% | 47.4–55.3 |
| `LNEUT=60` | 139 | 47.3% | 43.2–50.7 |
| `LNEUT=120:FLEAD=100` | 160 | 48.8% | 45.3–52.2 |
| `LPROXN=300` (partial) | 85 | 50.3% | 44.7–56.4 |

Not run: `LHPW`, `LEXPW`, `LNEAR=300`, `LPROXN=300:LNEAR=300`, `LNEUT=200`.
**No candidate shows a gain.**

## 5. Next steps

1. Fix the stalled leader directly: make leader progress toward a neutral it reaches first
   worth far more than ~17 points, and stop cultists shooting neutrals the enemy leader is
   not about to convert. Check against the traced positions before any match run.
2. Decide on the leader bonus (420 vs 0) using boss replays, since the arena fell after
   removing it.
3. Gate: self-play screen first, then the 14 boss replays (both seats), within the 6 h cap.
4. Submit only a candidate that improves boss replays without losing self-play.

## Files

`arena-41710268/` (battles, 82 game records, `games.jsonl.gz`), `battles-*.json`,
`battles_all.json`, `boss/` (replay logs `base-*`, `ln120-*`; `base_replays.jsonl.gz`),
`bot_x.c`, `bosstest.py`, `cgapi.py`, `fetch_new.py`, `sweep.sh`, `chunk.sh`,
`gate_games.json`. Self-play match files stayed in the session workspace; the numbers are above.
