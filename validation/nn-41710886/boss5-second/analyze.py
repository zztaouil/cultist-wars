import json,re,subprocess,statistics
from pathlib import Path
R=Path(__file__).resolve().parent;games={};inp=[]
for p in sorted(R.glob('replay-*.json')):
 d=json.loads(p.read_text());s=d['log'];gid=str(d['test_game_id']);a=re.findall(r'^  stdout: (.*)$',s,re.M);a=[' '.join(x.split()[:1 if x.startswith('WAIT') else 4 if ' MOVE ' in x else 3]) for x in a]
 scores=list(map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',s).groups()));stats=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(-?\d+)',l)) for l in s.splitlines() if 'stderr: depth=' in l]
 games[gid]={'seed':int(re.search(r'options: seed=(-?\d+)',s)[1]),'game':gid,'scores':scores,'result':'W' if scores[1]>scores[0] else 'D' if scores[0]==scores[1] else 'L','median_depth':statistics.median(x['depth'] for x in stats),'max_elapsed_us':max(x['elapsed_us'] for x in stats),'rng_zero_turns':sum(x['rng']==0 for x in stats),'timeout':bool(re.search(r'timeout|not provided|invalid action',s,re.I)),'eval_every_10_turns':[(2*i+1,x['advantage'],x['depth']) for i,x in enumerate(stats) if i%5==0]}
 inp.append(f"G {gid} {games[gid]['seed']} {len(a)}");inp.extend(a);(R/(gid+'.txt')).write_text(s)
r=subprocess.run(['java','-cp',str(R.parent/'build/java'),'model.Batch'],input='\n'.join(inp)+'\n',text=True,capture_output=True,check=True);(R/'recon.txt').write_text(r.stdout)
for line in r.stdout.splitlines():
 if line.startswith('G '):gid=line.split()[1];states=[];actions=[]
 elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s);actions.append(a)
 elif line.startswith('X '):raise RuntimeError(line)
 elif line=='E':
  g=games[gid];counts=lambda s:[sum(s[4*i+1]>0 and s[4*i+2]==side for i in range(14)) for side in (0,1)]
  assert counts(states[-1])==g['scores']
  events=[];metrics=[{'neutral_conversions':0,'enemy_conversions':0,'kills':0,'neutral_kills':0,'waits':0} for _ in range(2)]
  for t,(s,nxt) in enumerate(zip(states,states[1:])):
   side=s[56];a=actions[t]
   if a[0]==0:metrics[side]['waits']+=1
   for i in range(14):
    hp,owner=s[4*i+1:4*i+3];nh,no=nxt[4*i+1:4*i+3]
    if hp<=0:continue
    if owner!=no:
     metrics[side]['neutral_conversions' if owner==2 else 'enemy_conversions']+=1;events.append((t,side,'convert',i,owner,counts(nxt)))
    if nh==0:
     if owner==2:metrics[side]['neutral_kills']+=1
     elif owner!=side:metrics[side]['kills']+=1
     events.append((t,side,'kill',i,owner,counts(nxt)))
  g['metrics']=metrics;g['events']=events;g['counts_by_turn']={str(t):counts(states[t]) for t in (20,40,60,80,100,120,150) if t<len(states)}
(R/'summary.json').write_text(json.dumps(list(games.values()),indent=2))
for g in games.values():print(json.dumps(g))
