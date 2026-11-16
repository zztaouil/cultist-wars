"""Generate disjoint self-play shards using a frozen teacher and fixed seeds."""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent

def generate(job):
    split, shard, seed, games, nodes = job
    path = ROOT / 'data' / f'{split}_{shard:02d}.bin'
    if path.exists():
        raise FileExistsError(path)
    env = dict(os.environ, CW_NET=str(ROOT / 'teacher.bin'),
               CW_GEN=f'{games}:{nodes}:{seed}:{path}')
    result = subprocess.run([str(ROOT / 'build/gen')], env=env,
                            capture_output=True, text=True, check=True)
    print(result.stderr.strip(), flush=True)
    return dict(split=split, shard=shard, seed=seed, games=games, nodes=nodes,
                positions=path.stat().st_size // 48,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--train-shards', type=int, default=32)
    ap.add_argument('--holdout-shards', type=int, default=6)
    ap.add_argument('--games-per-shard', type=int, default=8)
    ap.add_argument('--nodes', type=int, default=8000)
    ap.add_argument('--workers', type=int, default=4)
    args = ap.parse_args()
    assert 0 < args.train_shards < 100000 and 0 < args.holdout_shards < 100000
    assert args.games_per_shard > 0 and args.nodes > 0 and args.workers > 0
    (ROOT / 'data').mkdir(exist_ok=True)
    jobs = [(split, i, start + i, args.games_per_shard, args.nodes)
            for split, count, start in [('train', args.train_shards, 141100),
            ('val', args.holdout_shards, 241100), ('test', args.holdout_shards, 341100)]
            for i in range(count)]
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        records = list(pool.map(generate, jobs))
    (ROOT / 'generation.json').write_text(json.dumps(records, indent=2) + '\n')
