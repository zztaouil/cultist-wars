"""Independent Java replay and recruitment/combat accounting for finished tests."""
import json,re,subprocess
from pathlib import Path
R=Path(__file__).resolve().parent;games={};inp=[]
for p in sorted((R/'boss').glob('seat*.json')):
    d=json.loads(p.read_text())
    if d.get('status')!='done':continue
    log=d['log'];gid=str(d['test_game_id']);seed=re.search(r'^options: seed=(-?\d+)',log,re.M)[1]
    actions=re.findall(r'^  stdout: (.*)$',log,re.M)
    actions=[' '.join(x.split()[:1 if x.startswith('WAIT') else 4 if ' MOVE ' in x else 3]) for x in actions]
    scores=list(map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',log).groups()))
    games[gid]=dict(case=p.stem,scores=scores,actions=len(actions))
    inp.append(f'G {gid} {seed} {len(actions)}');inp.extend(actions)
proc=subprocess.run(['java','-cp',str(R.parent/'nn-41710886/build/java'),'model.Batch'],input='\n'.join(inp)+'\n',text=True,capture_output=True,check=True)
(R/'boss/recon.txt').write_text(proc.stdout)
for line in proc.stdout.splitlines():
    if line.startswith('G '):gid=line.split()[1];states=[]
    elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s)
    elif line.startswith('X '):raise RuntimeError(line)
    elif line=='E':
        g=games[gid];counts=lambda s:[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in (0,1)]
        assert counts(states[-1])==g['scores'],g
        metrics=[dict(recruits=0,enemy_converts=0,kills=0,neutral_kills=0) for _ in range(2)]
        for s,nxt in zip(states,states[1:]):
            side=s[56]
            for i in range(14):
                hp,owner=s[4*i+1:4*i+3];nh,no=nxt[4*i+1:4*i+3]
                if hp<=0:continue
                if owner!=no:metrics[side]['recruits' if owner==2 else 'enemy_converts']+=1
                if nh==0:
                    if owner==2:metrics[side]['neutral_kills']+=1
                    elif owner!=side:metrics[side]['kills']+=1
        g['metrics']=metrics;g['verified']=True
(R/'boss/reconstruction.json').write_text(json.dumps(list(games.values()),indent=2))
print(json.dumps(dict(games=len(games),transitions=sum(g['actions'] for g in games.values()),all_final_counts_match=True)))
