"""Horizon-reversal census at OUR decision points. Judge: same frozen engine/eval, fixed depth 6 vs 8, fresh caches, exact RNG.
rev = value at depth 8 of (depth-8 best) minus value at depth 8 of (depth-6 best). act_loss8 = d8-best minus in-game move at depth 8."""
import json,sys,os,time,eng
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
seen={};order={'41711135':0,'41709358':1}
for g in sorted(G,key=lambda g:order.get(g['version'],2)):seen.setdefault((g['seed'],g['boss_seat']),g)
G=list(seen.values());out='horizon.jsonl';done=set()
if os.path.exists(out):
    for l in open(out):o=json.loads(l);done.add((o['id'],o['t']))
T0=time.time();LO,HI,STEP=int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3]);K=int(sys.argv[4]);M=int(sys.argv[5])
with open(out,'a') as f:
    for gi,g in enumerate(G):
        if gi%M!=K:continue
        eng.setup(g['walls']);my=1-g['boss_seat']
        for t in range(LO,min(HI,len(g['acts'])-1)):
            if t%2!=my or ((t-my)//2)%STEP or (g['id'],t) in done:continue
            if time.time()-T0>140:sys.exit(0)
            s=g['states'][t];a=tuple(g['acts'][t])
            b6,v6=eng.fixed(s,6);b8,v8=eng.fixed(s,8,0,25000)
            if b6 is None or b8 is None:
                f.write(json.dumps(dict(id=g['id'],t=t,incomplete=True))+'\n');f.flush();continue
            x=eng.one(s,b6,8) if b6!=b8 else v8;y=eng.one(s,a,8) if a not in (b6,b8) else (v8 if a==b8 else x)
            f.write(json.dumps(dict(id=g['id'],t=t,seat=my,ver=g['version'],actual=a,b6=b6,v6=v6,b8=b8,v8=v8,b6_at8=x,act_at8=y))+'\n');f.flush()
print('complete')
