"""Opportunities to hit the enemy leader vs how often each side takes them. Baseline-family games, by phase.
Also: what the mover did instead, and alternative-shot availability (kill shots)."""
import json,eng
from collections import Counter,defaultdict
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
st=defaultdict(Counter);rows=[]
for g in G:
    my=1-g['boss_seat'];eng.setup(g['walls'])
    for t in range(len(g['states'])-1):
        s=g['states'][t];side=t%2;who='us' if side==my else 'boss';opp=1-side
        if s[4*opp+1]<=0:continue
        L=eng.legal(s);a=tuple(g['acts'][t])
        ls=[x for x in L if x[0]==2 and x[3]==opp]           # shots whose victim is enemy leader
        if not ls:continue
        best=max(x[4] for x in ls);ph=0 if t<20 else 1 if t<40 else 2 if t<70 else 3 if t<100 else 4
        took=any(x[:3]==a for x in ls)
        killshot_other=any(x[0]==2 and x[3]>=0 and x[3]!=opp and s[4*x[3]+2]==opp and x[4]>=s[4*x[3]+1] for x in L)
        conv=any(x[0]==3 for x in L)
        k=(who,ph);st[k]['opp']+=1;st[k]['took']+=took;st[k]['bestdmg']+=best
        st[k]['kill_avail']+=best>=s[4*opp+1]
        if not took:
            kind=['WAIT','MOVE','SHOOT','CONVERT'][a[0]];lead=a[0]==1 and a[1]==side
            st[k]['alt_'+('LMOVE' if lead else kind)]+=1
        rows.append(dict(id=g['id'],t=t,who=who,took=took,best=best,lhp=s[4*opp+1],act=a,seat_us=my,killother=killshot_other,conv=conv))
json.dump(rows,open('leadershot_rows.json','w'))
names=['0-19','20-39','40-69','70-99','100+']
for who in ('us','boss'):
    print(who)
    for ph in range(5):
        c=st[(who,ph)]
        if c['opp']:print('  t %-6s turns-with-leader-shot %4d (%.1f/game)  taken %3.0f%%  mean best dmg %.1f  leader-kill available %d | instead: %s'%(names[ph],c['opp'],c['opp']/len(G),100*c['took']/c['opp'],c['bestdmg']/c['opp'],c['kill_avail'],{k[4:]:v for k,v in c.items() if k.startswith('alt_')}))
# by damage size
for who in ('us','boss'):
    for lo,hi in ((1,2),(3,4),(5,6)):
        R=[r for r in rows if r['who']==who and lo<=r['best']<=hi and r['t']<70 and r['best']<r['lhp']]
        print(who,'t<70 non-lethal best dmg %d-%d: n=%d taken %.0f%%'%(lo,hi,len(R),100*sum(r['took'] for r in R)/max(1,len(R))))
