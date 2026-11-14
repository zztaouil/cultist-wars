import json,random,sys
from pathlib import Path
D=Path(__file__).resolve().parent
out={}
for name in sys.argv[1:]:
 rows=[json.loads(l) for l in (D/name).read_text().splitlines()];groups={}
 for r in rows:groups.setdefault(r['seed'],[]).append((r['new']>r['baseline'])+.5*(r['new']==r['baseline']))
 assert all(len(v)==2 for v in groups.values()), 'Only summarize complete seat pairs'
 pairs=[sum(v)/len(v) for v in groups.values()];rng=random.Random(9301);boots=sorted(sum(rng.choices(pairs,k=len(pairs)))/len(pairs) for _ in range(20000))
 stats=[[r for g in rows for r in g['stats'][k]] for k in (0,1)]
 out[name]=dict(games=len(rows),wdl=[sum(r['new']>r['baseline'] for r in rows),sum(r['new']==r['baseline'] for r in rows),sum(r['new']<r['baseline'] for r in rows)],score=sum(pairs)/len(pairs),paired_ci95=[boots[500],boots[19500]],baseline_candidate=[dict(turns=len(ss),timeouts_proxy=sum(s['ns']>50000000 for s in ss),max_ms=max(s['ns'] for s in ss)/1e6,qshare=sum(s['qnodes'] for s in ss)/sum(s['nodes'] for s in ss),mean_depth=sum(s['depth'] for s in ss)/len(ss)) for ss in stats])
print(json.dumps(out,indent=2))
