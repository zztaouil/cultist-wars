"""Collect final evidence and checksums without choosing on test/match outcomes."""
import hashlib,json
from pathlib import Path
R=Path(__file__).resolve().parent;root=R.parents[1]
def read(name):return json.loads((R/name).read_text())
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
stages=['warm','fast','wide','fine-small','fine-wide'];hist={n:read(f'{n}/history.json') for n in stages}
compute=dict(completed_epochs={n:len(h) for n,h in hist.items()},completed_example_presentations=sum(len(h) for h in hist.values())*5000000,sum_epoch_worker_seconds=round(sum(v['seconds'] for h in hist.values() for v in h),2))
birth=getattr((R/'train-warm.log').stat(),'st_birthtime',None)
if birth:compute['training_wall_seconds']=round(max((R/n/'report.json').stat().st_mtime for n in ['fine-small','fine-wide'])-birth,2)
models=dict(baseline_float=dict(validation=read('warm/initial.json'),test=read('previous-policy-test.json')['test']),small_float=read('fine-small/report.json'),wide_float=read('fine-wide/report.json'),small_fp16=read('parity.json'),wide_row_int8=read('wide-parity.json'),wide_group_int8=read('wide-group-parity.json'))
matches={n:read(f) for n,f in [('small_vs_baseline','match-summary.json'),('wide_row_vs_baseline','wide49-match-summary.json'),('small_tree_vs_small','tree49-match-summary.json'),('wide_group_vs_baseline','group49-match-summary.json'),('wide_group_tree_vs_group','grouptree49-match-summary.json')]}
sources={n:dict(bytes=(R/f'{n}.c').stat().st_size,sha256=sha(R/f'{n}.c')) for n in ['baseline','candidate','wide','tree','wide-group','group-tree']}
assert all(v['bytes']<100000 for v in sources.values())
summary=dict(compute=compute,models=models,selection=read('final-selection.json'),matches=matches,sources=sources,production_unchanged=sha(root/'bot.c')==sources['baseline']['sha256'],search_cost=read('search-cost.json'),group_search_cost=read('group-search-cost.json'),checks=dict(engine_block=read('engine-block-parity.json'),disabled=read('disabled-parity.json'),tree=read('tree-parity.json'),group_tree=read('group-tree-parity.json'),runtime=read('runtime.json'),group_runtime=read('group-runtime.json')),limitations=['5M inputs include 3,371,876 ID-relabeled variants; 1,628,124 distinct training tactical boards.','Validation/test maps reused in prior research; original-only and map-separated from training.','Matches use the same 64 fresh maps across five exploratory comparisons, paired seats, exact RNG, same-family simulator.','Paired intervals are approximate 95% intervals; no judge timing or arena promotion claim.','Policy selected by quantized validation loss, not test or match results.'])
(R/'summary.json').write_text(json.dumps(summary,indent=2))
files={}
for path in sorted(R.rglob('*')):
 if not path.is_file() or 'build' in path.relative_to(R).parts or '__pycache__' in path.relative_to(R).parts:continue
 if path.name=='manifest.json' or path.suffix not in ['.py','.c','.pt','.npz','.json','.jsonl','.md','.txt']:continue
 files[str(path.relative_to(R))]=sha(path)
manifest=dict(files_sha256=files,dataset_manifest_sha256=sha(root/'validation/imitation-5m-20261114/manifest.json'),dataset_audit_sha256=sha(root/'validation/imitation-5m-20261114/cache/audit.json'))
(R/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(dict(compute=compute,selection=summary['selection'],matches=matches,production_unchanged=summary['production_unchanged'])),flush=True)
