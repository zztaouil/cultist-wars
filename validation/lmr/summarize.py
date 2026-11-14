"""Aggregate logs and paired-seed bootstrap uncertainty (draw = half point)."""
import json,random,sys
from pathlib import Path
D=Path(__file__).resolve().parent
out={}
for name in ['development49','holdout49']:
 path=D/(name+'.jsonl')
 if not path.exists():continue
 rows=[json.loads(l) for l in path.read_text().splitlines()];pairs={}
 for r in rows:pairs.setdefault(r['seed'],[]).append((1+(r['new']>r['baseline'])-(r['new']<r['baseline']))/2)
 pair_scores=[sum(p)/len(p) for p in pairs.values()];rng=random.Random(20261113)
 samples=sorted(sum(rng.choices(pair_scores,k=len(pair_scores)))/len(pair_scores) for _ in range(20000)) if rows else [0]*20000
 result={'games':len(rows),'WDL':[sum(r['new']>r['baseline'] for r in rows),sum(r['new']==r['baseline'] for r in rows),sum(r['new']<r['baseline'] for r in rows)],'score':sum(pair_scores)/len(pair_scores) if rows else None,'paired_bootstrap_95':[samples[500],samples[19499]],'bots':[]}
 for k in (0,1):
  turns=[s for r in rows for s in r['stats'][k]];n=len(turns)
  result['bots'].append({'turns':n,'depth_mean':sum(x['depth'] for x in turns)/n if n else 0,'nodes_mean':sum(x['nodes'] for x in turns)/n if n else 0,'q_share':sum(x['qnodes'] for x in turns)/max(1,sum(x['nodes'] for x in turns)),'wall_over_50ms':sum(x['ns']>50000000 for x in turns),'max_ms':max((x['ns']/1e6 for x in turns),default=0),'metrics':{key:sum(x['metrics'][key] for x in turns) for key in ['reductions','researches','research_nodes','pvs','pvs_nodes','aspiration','aspiration_nodes','tt_cut','qtt_cut']}})
 out[name]=result
for name in ['diagnosis','fixed6','iterative-profile']:
 path=D/(name+'.jsonl')
 if not path.exists():continue
 rows=[json.loads(l) for l in path.read_text().splitlines()];result={}
 for v in sorted(set(x['variant'] for x in rows)):
  a=[x for x in rows if x['variant']==v];nodes=sum(x['nodes'] for x in a);ns=sum(x['ns'] for x in a)
  r={'positions':len(a),'nodes':nodes,'seconds':ns/1e9,'q_share':sum(x['qnodes'] for x in a)/nodes,'mean_depth':sum(x['depth'] for x in a)/len(a)}
  if 'metrics' in a[0]:
   r['metrics']={k:sum(x['metrics'][k] for x in a) for k in a[0]['metrics'] if not isinstance(a[0]['metrics'][k],list)}
   r['by_depth']={str(d):{k:sum(x['metrics'][k][d] for x in a) for k in ['main','expanded','generated','searched','beta','first']} for d in range(1,33) if sum(x['metrics']['main'][d] for x in a)}
   r['q_left']=[sum(x['metrics']['qleft'][d] for x in a) for d in range(5)]
  result[v]=r
 out[name]=result
print(json.dumps(out,indent=2))
