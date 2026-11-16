"""Prevent resuming a match tag with changed sources, maps, or simulator."""
import hashlib,json
from pathlib import Path
R=Path(__file__).resolve().parent;root=R.parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def guard(tag,a,b):
 data=dict(a=Path(a).name,b=Path(b).name,a_sha256=sha(a),b_sha256=sha(b),maps_sha256=sha(R/'match-maps.json'),budget_ms=49,parts=4,shuffle_seed=41723000,runner_sha256=sha(root/'tests/arena_match.py'),simulator_sha256=sha(root/'validation/arena-verify/scripts/harness.py'))
 path=R/f'{tag}-sources.json'
 if path.exists():
  old=json.loads(path.read_text());assert old['inputs']==data,f'{tag}: inputs changed; use a new match tag/output directory'
 else:
  path.write_text(json.dumps(dict(inputs=data,recorded_after_start=bool(list(R.glob(f'match_{tag}_*.jsonl')))),indent=2))
