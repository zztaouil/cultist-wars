import json,re
from collections import Counter,defaultdict
G=[g for g in json.load(open('boss_games.json')) if g['me'] and g.get('stderr')]
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
rng=Counter();dep=defaultdict(list);over=0;n=0;late=Counter();games_amb=0
for g in G:
    if g['version'] not in BASE:continue
    amb=0
    for t,e in enumerate(g['stderr']):
        if not e:continue
        m=dict(re.findall(r'(\w+)=(-?\d+)',e))
        if 'rng' not in m:continue
        n+=1;r=int(m['rng']);rng[min(r,3)]+=1
        if r!=1 and t>3:late[r]+=1;amb=1
        dep[t//20*20].append(int(m['depth']))
        if int(m.get('elapsed_us',0))>50000:over+=1
    games_amb+=amb
print('games with logs',sum(1 for g in G if g['version'] in BASE),'our turns',n,'rng candidates',dict(rng),'non-singleton after turn 3:',dict(late),'games affected',games_amb,'elapsed>50ms turns',over)
print('mean completed depth by turn bucket',{k:round(sum(v)/len(v),1) for k,v in sorted(dep.items())})
