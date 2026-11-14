"""In-game search 'surprises': our logged root value falls by >=700 between consecutive own turns (2 plies).
Uses real judge stderr (actual 49-ms behaviour, real caches). Classifies the material event that follows within 6 plies
and counts how many plies before the first material loss the engine's value first reflected it."""
import json,re
from collections import Counter,defaultdict
G=[g for g in json.load(open('boss_games.json')) if g['me'] and g.get('stderr') and g['score_ok'] and g['version'] in ('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')]
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
tot=Counter();perres=defaultdict(list);ev=Counter();rows=[]
for g in G:
    my=1-g['boss_seat'];S=g['states'];adv={};dep={}
    for t,e in enumerate(g['stderr']):
        if e and t%2==my:
            m=re.search(r'depth=(\d+) advantage=(-?\d+)',e)
            if m:adv[t]=int(m.group(2));dep[t]=int(m.group(1))
    res=g['final'][my]-g['final'][1-my];n=0
    ts=sorted(adv)
    for a,b in zip(ts,ts[1:]):
        if b-a!=2 or abs(adv[a])>50000 or abs(adv[b])>50000:continue
        if adv[b]-adv[a]<=-700:
            n+=1
            # what material events happen in plies b-1 .. b+6 ?
            kinds=[]
            for t in range(a+1,min(b+7,len(S)-1)):
                x,y=S[t],S[t+1]
                for i in range(14):
                    if x[4*i+2]==2 and y[4*i+2]==1-my and y[4*i+1]>0:kinds.append('boss_conv_neutral')
                    if x[4*i+2]==my and y[4*i+2]==1-my and y[4*i+1]>0:kinds.append('boss_conv_ours')
                    if x[4*i+2]==my and x[4*i+1]>0 and y[4*i+1]==0:kinds.append('our_unit_killed')
            k='+'.join(sorted(set(kinds))) or 'none_within_6'
            already=(cnt(S[b],my)-cnt(S[b],1-my))<(cnt(S[a],my)-cnt(S[a],1-my))
            ev[(k,'already happened' if already else 'foreseen<=6 plies ahead')]+=1
            rows.append(dict(id=g['id'],ver=g['version'],seat=my,t=b,frm=adv[a],to=adv[b],depth=dep[a],kinds=k,already=already,res=res))
    perres[('seat%d'%my,'W' if res>0 else 'D' if res==0 else 'L')].append(n)
print('games',len(G),'surprises',len(rows))
for k,v in sorted(perres.items()):print(k,'games %d surprises/game %.2f'%(len(v),sum(v)/len(v)))
for k,v in ev.most_common():print(' ',k,v)
print('by phase',Counter(min(r['t']//20*20,100) for r in rows))
print('depth at turn before surprise: mean %.1f'%(sum(r['depth'] for r in rows)/len(rows)))
json.dump(rows,open('surprise_rows.json','w'))
# first surprise per LOST game
first={}
for r in rows:
    if r['res']<0:first.setdefault(r['id'],r)
print('lost games with >=1 surprise: %d ; first-surprise turn histogram'%len(first),sorted(Counter(r['t']//10*10 for r in first.values()).items()))
print(' first-surprise event kinds in losses',Counter(r['kinds'] for r in first.values()).most_common())
