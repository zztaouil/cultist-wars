# Arena NN assessment — submission 41711135

Read-only API snapshot: 10% ranking complete, score 20.17, global rank 38/401,
Gold 19/30. Submitted C is byte-identical to integrated candidate.c.
10 completed logs: 8 wins, 0 draws, 2 losses; 1 additional battle pending.
Two wins against kurnevsky are opponent turn-zero timeouts; actual played games
are 6W/2L. No own timeout/error in inspected logs. Maximum own reported elapsed
49.079 ms; this is internal timing, not full judge overhead or safety proof.

Loss 921363527 against Peterpanda: second seat, 1–3. Arena evaluation +1024 at
turn 71 (depth 6), -1063 at turn 81 (depth 9). Unit 11 dies on turn 82;
material goes from 3–3 to 3–2. Local fresh-cache 49-ms probes at six positions
(turns 71,73,75,77,79,81), three repetitions per bot, returned identical baseline
and NN actions/values/depths at each position. Both differ from some historical
arena choices. Hardware, cache/history and completed depth differ; probes are
not counterfactual replay evidence that baseline would win.

Loss 921364554 against Miek93: second seat, 3–4. Evaluation +1882 at turn 51,
+1397 at 61, -758 at 71, followed by persistent deficit and terminal loss.
Candidate also beat same opponent 3–0 in first seat on a different map.

Interpretation: early healthy execution, no demonstrated NN strength gain.
Opponent mix, seats/maps, and two free wins confound raw win rate. Baseline
history has 93 battles and different schedule; no direct percentage comparison.
Local prior matched pilot remains 50% (32W/16D/32L).

Next priority: reconstruct midgame loss decisions with persistent protocol
history; baseline-vs-policy ablation at matched depth and equal time; identify
whether tactical horizon/selective pruning or leaf evaluation drives swings.
Only then test shallow policy inference or value learning. Do not expand NN or
train longer based solely on this small arena sample. No submissions or IDE
changes made during assessment.
