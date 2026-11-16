"""Prepare outcome-labelled positions; verify every game against frozen simulator."""
import argparse, ctypes as C, gzip, hashlib, json, time
from pathlib import Path
import numpy as np
from bridge import ROOT,State,Move,Walls,build

def map_key(w):
    variants=[]
    for t in range(4):
        a=[0]*91
        for pos,v in enumerate(w):a[(12-pos%13 if t&1 else pos%13)+13*(6-pos//13 if t&2 else pos//13)]=v
        variants.append(bytes(a))
    return hashlib.sha256(min(variants)).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--max-games',type=int,default=1000)
    p.add_argument('--out',type=Path,default=ROOT/'cache');args=p.parse_args()
    corpus=ROOT.parent/'imitation-1m-20261114';out=args.out;out.mkdir(exist_ok=False)
    games={str(g['id']):g for g in map(json.loads,gzip.open(corpus/'games.jsonl.gz','rt'))}
    experts={r['nickname'] for r in json.loads((corpus/'leaderboard.json').read_text())['rows']}
    lib=build();sx=(C.c_float*208)();ax=(C.c_float*(253*32))();moves=(C.c_int*(253*3))()
    states_out=[];offsets=[0];labels=[];values=[];splits=[];records=[]
    report=dict(accepted=0,rejected=0,transitions=0);start=time.monotonic()
    with (out/'actions.f32').open('wb') as af:
        def consume(gid,w,states,acts,bad):
            g=games[gid]
            final=[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in (0,1)] if (s:=states[-1] if states else None) else []
            if bad or len(states)!=len(g['actions'])+1 or final!=g['scores'] or not states or not lib.is_terminal(State(*states[-1])):
                report['rejected']+=1;return
            lib.setup(Walls(*w))
            if not all(lib.verify(State(*s),Move(*a,0),State(*n)) for s,a,n in zip(states,acts,states[1:])):
                report['rejected']+=1;return
            report['accepted']+=1;report['transitions']+=len(states)-1
            key=map_key(w);bucket=int(key[:8],16)%100;split=0 if bucket<80 else 1 if bucket<90 else 2
            names={a['index']:a['pseudo'] for a in g['agents']};outcome=(final[0]>final[1])-(final[0]<final[1])
            for turn,(s,a) in enumerate(zip(states[:-1],acts)):
                if names[s[56]] not in experts:continue
                n=lib.extract(State(*s),Move(*a,0),sx,ax,moves)
                if n<1:continue
                mm=np.ctypeslib.as_array(moves)[:n*3].reshape(n,3)
                matches=np.flatnonzero(np.all(mm==a,axis=1)) if a[0] else np.flatnonzero(mm[:,0]==0)
                if len(matches)!=1:continue
                states_out.append(np.ctypeslib.as_array(sx).copy());af.write(C.string_at(ax,n*32*4));offsets.append(offsets[-1]+n)
                labels.append(int(matches[0]));values.append(outcome*(1 if s[56]==0 else -1));splits.append(split)
                records.append(dict(game=gid,turn=turn,map=key,split=split))
        for line in (corpus/'build/recon.txt').open():
            if line.startswith('G '):_,gid,ws=line.split();w=list(map(int,ws));states=[];acts=[];bad=False
            elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s);acts.append(a)
            elif line.startswith('X '):bad=True
            elif line.startswith('E'):
                consume(gid,w,states,acts,bad)
                if report['accepted']>=args.max_games:break
    np.save(out/'states.npy',np.asarray(states_out,dtype=np.float32))
    np.savez(out/'targets.npz',offsets=np.array(offsets),labels=np.array(labels),values=np.array(values,dtype=np.float32),splits=np.array(splits))
    (out/'records.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records))
    report.update(positions=len(labels),split_positions=[splits.count(i) for i in range(3)],maps=len({r['map'] for r in records}),feature_version=1,seconds=time.monotonic()-start,baseline_sha256=hashlib.sha256((ROOT/'baseline.c').read_bytes()).hexdigest(),notes=['Expert turns only; all complete-game transitions verified.','Map split inherited from prior experiments, not globally untouched.','No reflection augmentation: hidden RNG trajectories need not preserve reflection.'])
    (out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
    assert all(splits.count(i)>0 for i in range(3))
if __name__=='__main__':main()
