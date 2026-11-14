"""Symmetric short-horizon error audit. Judge = our frozen engine, depth-6 child values, fresh caches, exact RNG.
'flag' = actual move >=300 below engine-best at depth 6. Compare boss's moves vs our own in-game moves, and realised aftermath."""
import json
from collections import defaultdict,Counter
G={g['id']:g for g in json.load(open('boss_games.json'))}
def load(fs):
    R={}
    for f in fs:
        for l in open(f):o=json.loads(l);R[(o['id'],o['t'])]=o
    return [r for r in R.values() if r['v_boss'] is not None and abs(r['v_best'])<500000 and abs(r['v_boss'])<500000]
B=load(['bosspos.jsonl','bosspos-mid.jsonl','bosspos-late.jsonl']);U=load(['bosspos-us-a.jsonl','bosspos-us-b.jsonl','bosspos-us-c.jsonl'])
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def d(r,k):
    g=G[r['id']];S=g['states'];b=r['boss_seat'];t=r['t'];u=min(t+k,len(S)-1);return (cnt(S[u],b)-cnt(S[u],1-b))-(cnt(S[t],b)-cnt(S[t],1-b))
def cls(a,seat):return ['WAIT','CMOVE','SHOOT','CONVERT'][a[0]] if not(a[0]==1 and a[1]==seat) else 'LMOVE'
bins=((6,20),(20,40),(40,60),(60,80),(80,112))
for name,R in (('BOSS moves',B),('OUR in-game moves',U)):
    print(name,'n=%d'%len(R))
    for lo,hi in bins:
        X=[r for r in R if lo<=r['t']<hi];F=[r for r in X if r['v_best']-r['v_boss']>=300];N=[r for r in X if r['v_best']-r['v_boss']<300]
        if X:print('  t %3d-%3d n=%4d flagged %4.1f%% | aftermath +12 plies: flagged %+.2f, others %+.2f | +40: flagged %+.2f, others %+.2f | flagged classes %s'%(lo,hi,len(X),100*len(F)/len(X),sum(d(r,12) for r in F)/max(1,len(F)),sum(d(r,12) for r in N)/max(1,len(N)),sum(d(r,40) for r in F)/max(1,len(F)),sum(d(r,40) for r in N)/max(1,len(N)),dict(Counter(cls(r['boss'],r['boss_seat']) for r in F))))
    F=[r for r in R if r['v_best']-r['v_boss']>=300];print('  total flagged %.1f%% ; games with >=1 flag %d/%d ; per game %.2f'%(100*len(F)/len(R),len({r['id'] for r in F}),len({r['id'] for r in R}),len(F)/len({r['id'] for r in R})))
# our flags by game result and seat
by=defaultdict(lambda:[0,0])
for r in U:
    g=G[r['id']];my=1-g['boss_seat'];m=g['final'][my]-g['final'][1-my];k=('seat%d'%my,'W' if m>0 else 'D' if m==0 else 'L');by[k][0]+=1;by[k][1]+=r['v_best']-r['v_boss']>=300
print('our flag rate by seat/result:',{k:'%.1f%% (n=%d)'%(100*v[1]/v[0],v[0]) for k,v in sorted(by.items())})
by=defaultdict(lambda:[0,0])
for r in B:
    g=G[r['id']];my=1-g['boss_seat'];m=g['final'][my]-g['final'][1-my];k=('ourseat%d'%my,'usW' if m>0 else 'D' if m==0 else 'usL');by[k][0]+=1;by[k][1]+=r['v_best']-r['v_boss']>=300
print('boss flag rate by our seat/result:',{k:'%.1f%% (n=%d)'%(100*v[1]/v[0],v[0]) for k,v in sorted(by.items())})
# was our flagged move also what fresh 49ms picks? (ours field = fresh 49ms choice)
F=[r for r in U if r['v_best']-r['v_boss']>=300]
print('our flagged in-game moves: fresh local 49ms repeats the in-game move in %d/%d; fresh49 itself >=300 below d6-best in %d'%(sum(tuple(r['boss'])==tuple(r['ours']) for r in F),len(F),sum((r['v_best']-(r['v_ours'] if r['v_ours'] is not None else r['v_best']))>=300 for r in F)))
print('our all moves: in-game == fresh local 49ms: %.1f%%'%(100*sum(tuple(r['boss'])==tuple(r['ours']) for r in U)/len(U)))
json.dump(sorted(F,key=lambda r:(r['id'],r['t'])),open('our_flagged.json','w'))
