import statistics,time
from experiment import *
def main():
    candidate();off=library('off');on=library('on',1)
    raw=list(map(json.loads,(REPO/'validation/arena-study/results/agree.jsonl').read_text().splitlines()));rows=raw[:80]
    results={'off':[],'on':[]};parity=0
    import torch
    from train import Net
    model=Net();model.load_state_dict(torch.load(ROOT/'model.pt',weights_only=True))
    for i,r in enumerate(rows):
        for name,lib in ([('off',off),('on',on)] if i%2 else [('on',on),('off',off)]):
            h.setup(lib,r['walls']);before=lib.calls();v=h.run(lib,r['state']);v['nn_calls']=lib.calls()-before;results[name].append(v)
        f=(C.c_float*16)();on.get_features(h.Array(*r['state']),f)
        with torch.no_grad():pred=model(torch.tensor([list(f)])).item()*(1 if r['state'][56]==0 else -1)
        err=abs(on.correction(h.Array(*r['state']))-int(pred));parity=max(parity,err)
    assert parity<=1
    report={n:dict(positions=len(v),median_nodes=statistics.median(r['nodes'] for r in v),median_depth=statistics.median(r['depth'] for r in v),median_ms=statistics.median(r['ns']/1e6 for r in v),max_ms=max(r['ns']/1e6 for r in v),median_nn_calls=statistics.median(r['nn_calls'] for r in v)) for n,v in results.items()}
    report['max_integer_parity_error']=parity;save('throughput.json',report);save('throughput-raw.json',results);print(json.dumps(report),flush=True)
    # Tactical accuracy: deep baseline judges both chosen actions; equal-valued alternatives count correct.
    teacher=library('tactical-teacher');tactical=[]
    for i,r in enumerate(rows[:24]):
        h.setup(teacher,r['walls']);ref=h.run(teacher,r['state'],3)
        row=dict(index=i,reference=ref)
        for name in ('off','on'):
            a=results[name][i]['action'];h.setup(teacher,r['walls']);value=h.child(teacher,r['state'],a,2)
            row[name]=dict(value=value,regret=max(0,ref['value']-value),matches_reference_value=value>=ref['value'])
        tactical.append(row)
    save('tactical.json',dict(note='Depth-three baseline agreement/regret proxy, not ground-truth optimal play.',positions=tactical,summary={n:dict(accurate=sum(r[n]['matches_reference_value'] for r in tactical),positions=len(tactical),mean_regret=statistics.mean(r[n]['regret'] for r in tactical)) for n in ('off','on')}))
    # Serial paired games avoid inter-worker contention. Exclude every training/validation/test map.
    excluded={r['map'] for r in json.loads((ROOT/'records.json').read_text())}
    maps=[r for r in json.loads((REPO/'validation/nn-5m-20261114/match-maps.json').read_text()) if map_key(r[1]) not in excluded][:8]
    assert len(maps)==8;save('match-maps.json',maps);games=[]
    for gid,w,initial in maps:
        for seat in (0,1):
            for lib in (off,on):h.setup(lib,w)
            s=list(initial);plies=0
            while True:
                counts=[sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1)]
                if not all(counts) or s[57]<=0:break
                a=h.run(on if s[56]==seat else off,s)['action'];s=h.step(off,s,a);plies+=1
            games.append(dict(map=gid,seat=seat,own=counts[seat],opponent=counts[1-seat],plies=plies));save('games.json',games)
            print(json.dumps(dict(completed_games=len(games))),flush=True)
    scores=[1 if r['own']>r['opponent'] else 0 if r['own']<r['opponent'] else .5 for r in games]
    summary=dict(wins=scores.count(1),draws=scores.count(.5),losses=scores.count(0),score=statistics.mean(scores),budget_ms=48,pairs=8,workers=1)
    save('matches.json',summary);print(json.dumps(summary),flush=True)
if __name__=='__main__':main()
