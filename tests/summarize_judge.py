"""Summarize emitted diagnostics for game turns 20..119 (both seats)."""
import json
from pathlib import Path
import re
import statistics
import sys

for name in sys.argv[1:]:
    data = json.loads(Path(name).read_text())
    log = data.get('log', '')
    rows = []
    for block in re.split(r'(?m)^frame ', log):
        turn = re.search(r'Turn: (\d+)', block)
        stats = re.search(r'depth=(\d+) advantage=(-?\d+) nodes=(\d+)', block)
        if turn and stats:
            rows.append({'turn':int(turn[1]),'depth':int(stats[1]),'visits':int(stats[3])})
    mid = [row for row in rows if 20 <= row['turn'] < 120]
    print(json.dumps({'file':name,'test_game_id':data.get('test_game_id'),
        'diagnostic_turns':len(rows),'midgame_turns':len(mid),
        'midgame_mean_visits':statistics.mean(r['visits'] for r in mid) if mid else None,
        'midgame_median_visits':statistics.median(r['visits'] for r in mid) if mid else None,
        'midgame_mean_depth':statistics.mean(r['depth'] for r in mid) if mid else None,
        'midgame_turns_at_least_120k':sum(r['visits'] >= 120000 for r in mid),
        'max_elapsed_us':max([int(x) for x in re.findall(r'elapsed_us=(\d+)',log)],default=None),
        'scores':re.findall(r'(?m)^  scores: (.*)',log),
        'errors':re.findall(r'(?mi)^.*(?:timeout|timed out|invalid action|error:).*$',log)}))
