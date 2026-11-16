import json,random,statistics,hashlib,itertools
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parents[1]
rows=[json.loads(x) for x in (D/'matches.jsonl').read_text().splitlines()];games=rows[:-1];summary=rows[-1]
seeds=sorted(set(g['seed'] for g in games));scores=[statistics.mean(1 if g['new']>g['baseline'] else .5 if g['new']==g['baseline'] else 0 for g in games if g['seed']==seed) for seed in seeds]
r=random.Random(614982);samples=sorted(statistics.mean(r.choices(scores,k=len(scores))) for _ in range(20000))
summary.update(score=statistics.mean(scores),pair_scores=scores,paired_bootstrap_95=[samples[500],samples[19499]],note='Exploratory eight-map paired bootstrap; small related-engine sample. No proven strength gain; speed gate failed.')
diff=[x-.5 for x in scores]; observed=abs(sum(diff)); summary['paired_signflip_two_sided_p']=sum(abs(sum(d*sign for d,sign in zip(diff,signs)))>=observed-1e-12 for signs in itertools.product([-1,1],repeat=len(diff)))/(2**len(diff))
(D/'match-summary.json').write_text(json.dumps(summary,indent=2))
manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in D.iterdir() if p.is_file() and p.name!='sha256.json' and p.suffix in ['.c','.py','.json','.jsonl','.md','.log']}
manifest['production_bot.c']=hashlib.sha256((R/'bot.c').read_bytes()).hexdigest();assert manifest['production_bot.c']==manifest['neutral.c']
(D/'sha256.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(summary,indent=2))
