import json,statistics,collections
from pathlib import Path
D=Path(__file__).resolve().parent
rows=json.loads((D/'screen.json').read_text());result={'fixtures':len(rows),'variants':{}}
for k in ('baseline','contest','extension','combined'):
 timed=[r['results'][k]['timed'] for r in rows];fixed=[r['results'][k]['fixed'] for r in rows]
 result['variants'][k]=dict(changed49=sum(r['results'][k]['timed']['action']!=r['results']['baseline']['timed']['action'] for r in rows),changed_recruit49=sum(r['kind']=='deficit_widens' and r['results'][k]['timed']['action']!=r['results']['baseline']['timed']['action'] for r in rows),mean_depth49=statistics.mean(x['depth'] for x in timed),fixed_nodes=sum(x['nodes'] for x in fixed),fixed_ms=sum(x['ms'] for x in fixed),fixed_incomplete=sum(x['stopped'] for x in fixed))
result['reversal']=next(r for r in rows if r['game']=='921365647' and r['turn']==19)
for filename in ('development-results.json','holdout-results.json'):
 if (D/filename).exists():result[filename]=json.loads((D/filename).read_text())
for filename in ('summary.json','summary-extra.json'):
 p=D/'boss'/filename
 if p.exists():
  bb=json.loads(p.read_text());bs={}
  for v in sorted({x['variant'] for x in bb}):
   rr=[x for x in bb if x['variant']==v];c=collections.Counter(x['result'] for x in rr)
   bs[v]=dict(games=len(rr),W=c['W'],D=c['D'],L=c['L'],score=(c['W']+.5*c['D'])/len(rr),errors=sum(x['error'] for x in rr),rng_zero=sum(x['rng_zero'] for x in rr),max_us=max(x['max_us'] for x in rr),seats={str(s):dict(collections.Counter(x['result'] for x in rr if x['seat']==s)) for s in (0,1)})
  result['boss-'+filename]=bs
(D/'summary.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k!='reversal'},indent=2))
