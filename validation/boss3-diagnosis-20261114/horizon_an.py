import json,sys
from collections import Counter,defaultdict
G={g['id']:g for g in json.load(open('boss_games.json'))}
R={}
for l in open(sys.argv[1] if len(sys.argv)>1 else 'horizon.jsonl'):
    o=json.loads(l)
    if not o.get('incomplete'):R[(o['id'],o['t'])]=o
R=[r for r in R.values() if None not in (r['b6_at8'],r['act_at8']) and max(abs(r['v8']),abs(r['b6_at8']),abs(r['act_at8']))<500000]
print('positions',len(R),'games',len({r['id'] for r in R}))
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def d(r,k):
    g=G[r['id']];S=g['states'];b=r['seat'];t=r['t'];u=min(t+k,len(S)-1);return (cnt(S[u],b)-cnt(S[u],1-b))-(cnt(S[t],b)-cnt(S[t],1-b))
def res(r):
    g=G[r['id']];m=g['final'][r['seat']]-g['final'][1-r['seat']];return 'W' if m>0 else 'D' if m==0 else 'L'
for r in R:r['rev']=r['v8']-r['b6_at8'];r['loss']=r['v8']-r['act_at8'];r['res']=res(r)
def rate(X,key,thr):return 100*sum(r[key]>=thr for r in X)/max(1,len(X))
print('depth-6 best is >=300 worse than depth-8 best (judged at depth 8): %.1f%% ; >=700: %.1f%%'%(rate(R,'rev',300),rate(R,'rev',700)))
print('in-game move >=300 worse than depth-8 best at depth 8: %.1f%% ; >=700: %.1f%%'%(rate(R,'loss',300),rate(R,'loss',700)))
for seat in (0,1):
    for rs in 'WDL':
        X=[r for r in R if r['seat']==seat and r['res']==rs]
        if X:print('  seat %d %s: n=%3d games=%2d  d6-reversal>=300 %.1f%%  in-game loss>=300 %.1f%% (per game %.2f)  >=700 %.1f%%'%(seat,rs,len(X),len({r['id'] for r in X}),rate(X,'rev',300),rate(X,'loss',300),sum(r['loss']>=300 for r in X)/len({r['id'] for r in X}),rate(X,'loss',700)))
for lo,hi in ((13,21),(21,29),(29,37),(37,45),(45,61),(61,81)):
    X=[r for r in R if lo<=r['t']<hi]
    if X:print('  t %d-%d n=%d reversal>=300 %.1f%% in-game loss>=300 %.1f%%'%(lo,hi,len(X),rate(X,'rev',300),rate(X,'loss',300)))
F=[r for r in R if r['loss']>=300];N=[r for r in R if r['loss']<300]
for k in (8,16,30):print(' aftermath +%d plies (our count diff change): flagged %+.2f (n=%d) vs unflagged %+.2f (n=%d)'%(k,sum(d(r,k) for r in F)/len(F),len(F),sum(d(r,k) for r in N)/len(N),len(N)))
def cls(a,seat):return ['WAIT','CMOVE','SHOOT','CONVERT'][a[0]] if not(a[0]==1 and a[1]==seat) else 'LMOVE'
print(' flagged: in-game class -> depth-8 class',Counter((cls(r['actual'],r['seat']),cls(r['b8'],r['seat'])) for r in F).most_common(12))
first={}
for r in sorted(F,key=lambda r:r['t']):first.setdefault(r['id'],r)
L=[r for r in first.values() if r['res']=='L'];print(' lost games in sample: %d, with >=1 flagged decision in window: %d ; first flagged turn'%(len({r['id'] for r in R if r['res']=='L'}),len(L)),sorted(r['t'] for r in L))
json.dump(sorted(F,key=lambda r:-r['loss']),open('horizon_flagged.json','w'))
