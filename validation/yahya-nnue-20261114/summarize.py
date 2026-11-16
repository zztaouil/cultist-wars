"""Report paired-map bootstrap uncertainty; candidate is always A."""
import hashlib
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent
results = {}
for name in ('teacher', 'production'):
    rows = [json.loads(line) for line in (ROOT / f'match-{name}.jsonl').read_text().splitlines()]
    summary = rows[-1]
    assert summary.get('summary') and summary['games'] == len(rows) - 1
    pairs = {}
    for row in rows[:-1]:
        pairs.setdefault(row['game'] // 2, []).append({'A': 1., 'B': 0., '-': .5}[row['winner']])
    assert all(len(pair) == 2 for pair in pairs.values())
    scores = np.array([np.mean(pair) for pair in pairs.values()])
    boot = np.random.default_rng(1411).choice(scores, size=(10000, len(scores))).mean(1)
    results[name] = dict(**summary, score=float(scores.mean()),
                         paired_bootstrap_95=np.quantile(boot, [.025, .975]).tolist())
files = ['baseline.cpp', 'engine.cpp', 'teacher.bin', 'candidate.bin', 'bot.cpp',
         'train.py', 'generate.py', 'tools/model.py', 'tools/pack.py', 'tools/referee.cpp']
results['sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files}
(ROOT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
