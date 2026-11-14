"""Is BOSS 3 deterministic given identical history? For games sharing seed+seat, find who deviates first."""
import json,itertools
from collections import defaultdict,Counter
G=json.load(open('boss_games.json'))
by=defaultdict(list)
for g in G:by[(g['seed'],g['boss_seat'])].append(g)
print('distinct (seed,seat):',len(by),'distinct seeds',len({g['seed'] for g in G}))
c=Counter();ex=[]
for k,gs in by.items():
    for a,b in itertools.combinations(gs,2):
        n=min(len(a['actions']),len(b['actions']))
        i=next((i for i in range(n) if a['actions'][i]!=b['actions'][i]),None)
        if i is None:c['identical']+=1;continue
        who='boss' if i%2==k[1] else 'us';c[who]+=1
        if who=='boss':ex.append((k,a['src'],b['src'],i,a['actions'][i],b['actions'][i]))
print(c)
for e in ex[:12]:print(e)
