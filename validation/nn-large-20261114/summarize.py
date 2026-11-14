"""Summarize matched boss differences without treating tiny sample as proof."""
import json, math, statistics
from pathlib import Path
R=Path(__file__).resolve().parent
rows=json.loads((R/'boss/summary.json').read_text());out={'games':len(rows),'variants':{}}
score={'W':1.,'D':.5,'L':0.}
for name in ('baseline','candidate'):
    rr=[r for r in rows if r['variant']==name]
    out['variants'][name]={'W':sum(r['result']=='W' for r in rr),'D':sum(r['result']=='D' for r in rr),'L':sum(r['result']=='L' for r in rr),'score':sum(score[r['result']] for r in rr)/len(rr),'errors':sum(r['error'] for r in rr),'max_internal_us':max(r['max_us'] for r in rr),'seats':{}}
    for seat in (0,1):
        sr=[r for r in rr if r['seat']==seat]
        out['variants'][name]['seats'][seat]={'games':len(sr),'W':sum(r['result']=='W' for r in sr),'D':sum(r['result']=='D' for r in sr),'L':sum(r['result']=='L' for r in sr)}
matched=[]
for case in dict.fromkeys(r['case'] for r in rows):
    pair={r['variant']:r for r in rows if r['case']==case}
    if len(pair)==2:
        b,c=pair['baseline'],pair['candidate'];s=c['seat']
        matched.append(dict(case=case,seat=s,baseline=b['result'],candidate=c['result'],score_delta=score[c['result']]-score[b['result']],margin_delta=(c['scores'][s]-c['scores'][1-s])-(b['scores'][s]-b['scores'][1-s])))
out['paired_cases']=matched;d=[r['score_delta'] for r in matched]
if len(d)>1:
    mean=statistics.mean(d);se=statistics.stdev(d)/math.sqrt(len(d));out['paired_score_delta']=mean;out['approximate_95_ci']=[mean-1.96*se,mean+1.96*se]
out['limits']='Eight-case pilot, repeated historical second-seat maps and fresh first-seat maps. Seat samples differ. Approximate normal interval with few cases; not proof of strength or arena promotion.'
(R/'boss-results.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
