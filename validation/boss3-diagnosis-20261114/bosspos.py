"""Put our frozen engine (41711135) in BOSS 3's seat at BOSS's actual decision points (exact RNG from Java states).
Records: boss action, our 49-ms fresh-cache choice, depth-6 child value of each (same search, full window).
Fresh caches/history: NOT identical to in-game state; single timed sample. Resumable."""
import json,sys,os,time,eng
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
# one game per distinct (seed,seat) to avoid replay duplication; prefer 41711135 then others
seen={};order={'41711135':0,'41709358':1}
for g in sorted(G,key=lambda g:order.get(g['version'],2)):seen.setdefault((g['seed'],g['boss_seat']),g)
G=list(seen.values())
out='bosspos-%s.jsonl'%sys.argv[4] if len(sys.argv)>4 else 'bosspos.jsonl';done=set()
if os.path.exists(out):
    for l in open(out):o=json.loads(l);done.add((o['id'],o['t']))
T0=time.time();LO,HI=int(sys.argv[1]),int(sys.argv[2]);STEP=int(sys.argv[3]) if len(sys.argv)>3 else 2
with open(out,'a') as f:
    for g in G:
        eng.setup(g['walls']);bs=g['boss_seat']
        for t in range(bs+ (0 if (LO-bs)%2==0 else 0),min(HI,len(g['acts'])-1)):
            if t%2!=bs or t<LO or (t//2)%STEP or (g['id'],t) in done:continue
            if time.time()-T0>150:sys.exit(0)
            s=g['states'][t];a=tuple(g['acts'][t]);cv=eng.child_values(s,6,0,4000)
            if cv is None:continue
            ch,v,d,n=eng.timed(s,49)
            best=max(cv.items(),key=lambda x:x[1])
            f.write(json.dumps(dict(id=g['id'],t=t,boss_seat=bs,boss=a,ours=ch,ours_depth=d,v_boss=cv.get(a),v_ours=cv.get(ch),best=best[0],v_best=best[1],lead=bool(a[0] and a[1]==bs),n=len(cv)))+'\n');f.flush()
print('complete')
