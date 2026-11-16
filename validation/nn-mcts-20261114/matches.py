"""Small development screen, equal total 48 ms, paired seats; not promotion holdout."""
import argparse,concurrent.futures,hashlib,json,math,random,statistics
from pathlib import Path
from verify import ROOT,REPO,library,h

def play(task):
    name,flags,maps,part=task
    a=library(f'match-{name}-{part}-a',baseline=True)
    b=library(f'match-{name}-{part}-b',flags)
    results=[]
    for gid,w,initial in maps:
        for seat in (0,1):
            for lib in (a,b):h.setup(lib,w)
            s=list(initial);plies=0
            while True:
                counts=[sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1)]
                if not all(counts) or s[57]<=0:break
                result=h.run(b if s[56]==seat else a,s);s=h.step(a,s,result['action']);plies+=1
            row=dict(variant=name,map=gid,seat=seat,own=counts[seat],opponent=counts[1-seat],plies=plies)
            results.append(row)
    return results

def main():
    p=argparse.ArgumentParser();p.add_argument('--pairs',type=int,default=8);p.add_argument('--workers',type=int,default=4);args=p.parse_args()
    out=ROOT/'screen';out.mkdir(exist_ok=False)
    maps=json.loads((ROOT.parent/'nn-5m-20261114/match-maps.json').read_text())[:args.pairs]
    variants={'puct-value':[],'puct-heuristic':['MCTS_VALUE=0'],'ab-value':['MCTS_SEARCH=0','AB_NEURAL_VALUE=1']}
    manifest=dict(pairs=args.pairs,workers=args.workers,budget_ms=48,variants=variants,source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['candidate.c','baseline.c']},maps=maps,notes=['Development maps reused from earlier experiment, not fresh final holdout.','Same-family exact-RNG simulator; no judge timeout enforcement.','Parallel CPU processes share machine; total elapsed-time deadlines remain active.'])
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    tasks=[(n,f,maps[part::args.workers],part) for n,f in variants.items() for part in range(args.workers)]
    allrows=[]
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.workers) as pool:
        for rows in pool.map(play,tasks):
            allrows.extend(rows);(out/'games.json').write_text(json.dumps(allrows,indent=2))
            print(json.dumps(dict(finished_games=len(allrows))),flush=True)
    summary={}
    for name in variants:
        rows=[r for r in allrows if r['variant']==name];scores=[1 if r['own']>r['opponent'] else 0 if r['own']<r['opponent'] else .5 for r in rows]
        paired={}
        for r,sc in zip(rows,scores):paired.setdefault(r['map'],[]).append(sc)
        means=[statistics.mean(x) for x in paired.values()];mean=statistics.mean(means);se=statistics.stdev(means)/math.sqrt(len(means))
        summary[name]=dict(games=len(rows),wins=scores.count(1),draws=scores.count(.5),losses=scores.count(0),score=mean,interval_note='Exploratory eight-pair screen; no confidence interval reported.')
    (out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary),flush=True)
if __name__=='__main__':main()
