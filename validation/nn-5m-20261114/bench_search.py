"""49 ms single-worker search costs; rotate variants for each archived position."""
import ctypes as C,importlib.util,json,statistics
from pathlib import Path
R=Path(__file__).resolve().parent;root=R.parents[1]
spec=importlib.util.spec_from_file_location('h',root/'validation/arena-verify/scripts/harness.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
names=['baseline','candidate','tree','wide'];libs={n:h.build(R/f'{n}.c',f'nn5m-cost-{n}',49) for n in names}
rows=list(map(json.loads,(root/'validation/arena-study/results/agree.jsonl').read_text().splitlines()))[:100]
data={n:[] for n in names}
for i,row in enumerate(rows):
 for n in names[i%len(names):]+names[:i%len(names)]:
  h.setup(libs[n],row['walls']);v=h.run(libs[n],row['state']);data[n].append(v)
result={}
for n,values in data.items():
 times=sorted(v['ns']/1e6 for v in values)
 result[n]=dict(positions=len(values),mean_depth=statistics.mean(v['depth'] for v in values),median_nodes=statistics.median(v['nodes'] for v in values),median_ms=statistics.median(times),p95_ms=times[int(.95*len(times))],max_ms=max(times))
(R/'search-cost.json').write_text(json.dumps(result,indent=2));(R/'search-cost-raw.json').write_text(json.dumps(data));print(json.dumps(result),flush=True)
