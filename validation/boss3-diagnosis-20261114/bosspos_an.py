import json
from collections import Counter,defaultdict
G={g['id']:g for g in json.load(open('boss_games.json'))}
R={}
import sys
for l in open(sys.argv[1]):
    o=json.loads(l);R[(o['id'],o['t'])]=o
R=list(R.values());print('positions',len(R),'games',len({r['id'] for r in R}))
def cls(a,seat):return ['WAIT','CMOVE','SHOOT','CONVERT'][a[0]] if not(a[0]==1 and a[1]==seat) else 'LMOVE'
agree=sum(tuple(r['boss'])==tuple(r['ours']) for r in R);print('exact agreement %.1f%%'%(100*agree/len(R)))
M=Counter((cls(r['boss'],r['boss_seat']),cls(r['ours'],r['boss_seat'])) for r in R)
cl=['LMOVE','CONVERT','SHOOT','CMOVE','WAIT']
print('rows=boss class, cols=our engine class in same position');print('%8s'%''+''.join('%8s'%c for c in cl))
for a in cl:print('%8s'%a+''.join('%8d'%M[(a,b)] for b in cl))
def med(x):x=sorted(x);return x[len(x)//2] if x else None
for ph,(lo,hi) in enumerate(((6,20),(20,32),(32,46),(46,62),(62,78),(78,112))):
    X=[r for r in R if lo<=r['t']<hi and tuple(r['boss'])!=tuple(r['ours']) and r['v_boss'] is not None and abs(r['v_best'])<500000]
    gaps=[r['v_best']-r['v_boss'] for r in X]
    print('t %d-%d disagreements %d: depth-6 gap (engine best - boss move) median %s, <50: %.0f%%, 50-300: %.0f%%, 300-800: %.0f%%, >800: %.0f%%'%(lo,hi,len(X),med(gaps),*[100*sum(a<=g<b for g in gaps)/len(gaps) for a,b in ((-1e9,50),(50,300),(300,800),(800,1e9))]))
# boss leader moves we'd reject strongly
X=[r for r in R if r['lead'] and tuple(r['boss'])!=tuple(r['ours']) and r['v_boss'] is not None and abs(r['v_best'])<500000]
gaps=[r['v_best']-r['v_boss'] for r in X];print('boss LEADER moves our engine would not play: n=%d median gap %s; gap>=300: %d; our alternative class:'%(len(X),med(gaps),sum(g>=300 for g in gaps)),Counter(cls(r['ours'],r['boss_seat']) for r in X))
big=sorted([r for r in R if r['v_boss'] is not None and abs(r['v_best'])<500000 and r['v_best']-r['v_boss']>=300],key=lambda r:-(r['v_best']-r['v_boss']))
print('boss moves our depth-6 search calls >=300 worse than its best: n=%d of %d (%.1f%%), by boss class'%(len(big),len(R),100*len(big)/len(R)),Counter(cls(r['boss'],r['boss_seat']) for r in big))
# outcome after such "errors": did boss lose material within 6/12 plies vs baseline?
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def delta(r,k):
    g=G[r['id']];S=g['states'];b=r['boss_seat'];t=r['t'];u=min(t+k,len(S)-1);return (cnt(S[u],b)-cnt(S[u],1-b))-(cnt(S[t],b)-cnt(S[t],1-b))
for k in (6,12,24,40):
    print(' count-diff change for boss after %2d plies: flagged(>=300) %+.2f | near-tie disagreements(<50) %+.2f | agreements %+.2f'%(k,sum(delta(r,k) for r in big)/len(big),(lambda X:sum(delta(r,k) for r in X)/len(X))([r for r in R if r['v_boss'] is not None and tuple(r['boss'])!=tuple(r['ours']) and r['v_best']-r['v_boss']<50]),(lambda X:sum(delta(r,k) for r in X)/len(X))([r for r in R if tuple(r['boss'])==tuple(r['ours'])])))
json.dump(big[:60],open(sys.argv[1]+'.flagged.json','w'))
for r in big[:12]:print(r['id'],r['t'],'boss',r['boss'],'ours',r['ours'],'gap',r['v_best']-r['v_boss'])
