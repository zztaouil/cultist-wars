"""Audit teacher provenance, ID-aware uniqueness, synthetic counts, splits, retention."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--cache', type=Path, default=ROOT/'cache')
parser.add_argument('--require-target', action='store_true')
parser.add_argument('--require-original', action='store_true')
args = parser.parse_args()
cache = args.cache
m = json.loads((cache/'meta.json').read_text())
r = json.loads((cache/'report.json').read_text())
o, y, splits = m['offsets'], m['labels'], m['splits']
assert len(o) == len(y)+1 and len(splits) == len(y) and o[0] == 0
assert all(b-a >= 2 and 0 <= label < b-a for a, b, label in zip(o, o[1:], y))
assert (cache/'features.f32').stat().st_size == o[-1]*128*4
permutations = m['unit_id_permutations']
assert permutations[0] == list(range(14))
assert all(p[:2] == [0, 1] and sorted(p) == list(range(14)) for p in permutations)
assert len({tuple(p) for p in permutations}) == len(permutations)
seen, tactical, train_tactical, identity_tactical, sources = set(), set(), set(), set(), set()
maps, players = {}, {}
counts, variants, perm_counts = [0]*3, [0]*4, [0]*len(permutations)
experts = {p['nickname'] for p in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
records_hash = hashlib.sha256()
original_positions = synthetic_positions = n = 0
for n, line in enumerate((cache/'records.jsonl').open(), 1):
    records_hash.update(line.encode())
    rec = json.loads(line)
    split, pid, reflection = splits[n-1], rec['id_permutation'], rec['reflection']
    assert split in (0, 1, 2) and 0 <= pid < len(permutations) and 0 <= reflection < 4
    assert rec['player'] in experts
    assert split == 0 or (reflection == 0 and pid == 0)
    assert rec['synthetic'] == bool(reflection or pid)
    digest, board = bytes.fromhex(rec['state_sha']), bytes.fromhex(rec['tactical_sha'])
    assert digest not in seen
    seen.add(digest)
    tactical.add(board)
    if split == 0:
        train_tactical.add(board)
    if pid == 0:
        identity_tactical.add(board)
    else:
        assert board in identity_tactical, 'Relabeled row lacks prior geometry-only source'
    maps.setdefault(rec['map'], set()).add(split)
    counts[split] += 1
    variants[reflection] += 1
    perm_counts[pid] += 1
    players[rec['player']] = players.get(rec['player'], 0)+1
    sources.add((int(rec['game']), rec['turn']))
    original_positions += int(not reflection and not pid)
    synthetic_positions += rec['synthetic']
assert n == len(y) == r['positions']
assert counts == r['split_positions'] and variants == r['reflection_counts']
assert perm_counts == r['permutation_counts'] and players == r['player_positions']
assert all(len(s) == 1 for s in maps.values())
assert original_positions == r['original_positions']
assert synthetic_positions == r['synthetic_positions']
assert sum(perm_counts[1:]) == r['id_relabelled_positions']
assert tactical == identity_tactical  # No claim that ID relabeling adds tactical boards.
old = ROOT.parent/'imitation-1m-20261114/cache/records.jsonl'
missing = sum(bytes.fromhex(json.loads(line)['state_sha']) not in seen for line in old.open())

def file_hash(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()

report = dict(positions=n, unique_observable_states_with_ids=len(seen), unique_tactical_states_ignoring_ids=len(tactical), training_tactical_states_ignoring_ids=len(train_tactical), source_game_turns=len(sources), original_played_positions=original_positions, synthetic_positions=synthetic_positions, id_relabelled_positions=sum(perm_counts[1:]), split_positions=counts, map_groups=len(maps), reflection_counts=variants, permutation_counts=perm_counts, players=players, map_leakage=False, duplicate_inputs_with_ids=False, heldouts_original_only=True, original_dataset_missing_states=missing, target_train_positions=r['target_train_positions'], target_reached=counts[0] == r['target_train_positions'], meta_sha256=file_hash(cache/'meta.json'), records_sha256=records_hash.hexdigest())
(cache/'audit.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report), flush=True)
if args.require_target:
    assert report['target_reached']
if args.require_original:
    assert missing == 0
