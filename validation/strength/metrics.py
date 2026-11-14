"""Summaries from retained raw data; no new matches or tuning."""
import json,random,re
from pathlib import Path
D=Path(__file__).resolve().parent
def rows(name):return [json.loads(l) for l in (D/name).read_text().splitlines()]
def save(name,obj):(D/name).write_text(json.dumps(obj,indent=2)+'\n')
x=rows('q-fixed-final.jsonl')
out={'fixed':dict(comparisons=len(x),parity=all(all(r['results'][0][k]==r['results'][1][k] for k in ['value','action','nodes','qnodes','stopped']) for r in x),elapsed_ns=[sum(r['results'][k]['ns'] for r in x) for k in (0,1)],visits=[sum(r['results'][k]['nodes'] for r in x) for k in (0,1)],per_repeat=[[sum(r['results'][k]['ns'] for r in x if r['repeat']==rep) for k in (0,1)] for rep in range(3)])}
for name in ['baseline','q']:
 x=rows(f'profile-{name}.jsonl');ns=sum(r['ns'] for r in x);cost={k:sum(r['costs'][k] for r in x) for k in x[0]['costs']}
 out['profile-'+name]=dict(total_ns=ns,costs=cost,q_visit_share=sum(r['qnodes'] for r in x)/sum(r['nodes'] for r in x),q_time_share=cost['q_ns']/ns)
x=rows('eval-cost-final.jsonl')
out['eval_ns_per_call']={str((k,c)):sum(r['ns'] for r in x if r['cold']==c and r['variant']==k)/sum(r['calls'] for r in x if r['cold']==c and r['variant']==k) for k in range(3) for c in (0,1)}
save('efficiency-summary.json',out)
out={}
for prefix,seeds in [('hp',range(2000,2004)),('q',range(4100,4104))]:
 groups={}
 for variant in ['baseline',prefix]:
  games=[];diags=[]
  for seed in seeds:
   log=json.loads((D/f'boss-{variant}-{seed}.json').read_text())['log']
   a,b=map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',log).groups())
   diags += [{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',l)} for l in log.splitlines() if 'depth=' in l]
   games.append(dict(seed=seed,units=[a,b],score=(a>b)+.5*(a==b),timeout='timeout!' in log,error='error:' in log))
  groups[variant]=dict(games=games,wdl=[sum(r['score']==v for r in games) for v in [1,.5,0]],turns=len(diags),qshare=sum(d.get('qnodes',0) for d in diags)/sum(d.get('nodes',0) for d in diags),max_elapsed_us=max(d.get('elapsed_us',0) for d in diags),ambiguous=sum(d.get('rng',1)!=1 for d in diags))
 deltas=[a['score']-b['score'] for a,b in zip(groups[prefix]['games'],groups['baseline']['games'])]
 rng=random.Random(73);bs=sorted(sum(rng.choices(deltas,k=4))/4 for _ in range(20000))
 groups['paired_score_delta']=sum(deltas)/4;groups['paired_delta_ci95']=[bs[500],bs[19500]];out[prefix]=groups
save('boss-summary.json',out)
print('Updated efficiency-summary.json and boss-summary.json')
