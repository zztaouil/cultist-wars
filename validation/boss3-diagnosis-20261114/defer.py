"""When a neutral CONVERT is legal: does the side take it immediately? If deferred: what instead, and who ends up with that neutral.
All valid games of our engine family vs BOSS 3 for boss behaviour (boss side); baseline family for ours."""
import json,eng
from collections import Counter,defaultdict
A=[g for g in json.load(open('boss_games.json')) if g['me'] and g['score_ok']]
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
st=defaultdict(Counter);ex=[]
for g in A:
    my=1-g['boss_seat'];eng.setup(g['walls']);S=g['states']
    for t in range(len(S)-1):
        side=t%2;who='us' if side==my else 'boss'
        if who=='us' and g['version'] not in BASE:continue
        s=S[t];a=tuple(g['acts'][t]);L=eng.legal(s);cv=[x for x in L if x[0]==3 and s[4*x[2]+2]==2]
        if not cv:continue
        ph='t<20' if t<20 else 't20-39' if t<40 else 't40+';k=(who,ph);st[k]['avail']+=1
        if a[0]==3:st[k]['took']+=1;continue
        cl='LMOVE' if a[0]==1 and a[1]==side else ['WAIT','CMOVE','SHOOT'][a[0]];st[k]['defer_'+cl]+=1
        # fate of the (first) skippable neutral
        i=cv[0][2];f='still_neutral_end'
        for u in range(t,len(S)-1):
            x,y=S[u],S[u+1]
            if y[4*i+1]==0:f='died';break
            if y[4*i+2]!=2:f='same_side_later' if y[4*i+2]==side else 'other_side';st[k]['delay_sum']+=(u-t) if y[4*i+2]==side else 0;break
        st[k]['fate_'+f]+=1
        if who=='boss' and cl=='LMOVE' and len(ex)<400:ex.append((g['id'],t,a))
for k in sorted(st):
    c=st[k];d=c['avail']-c['took']
    print(k,'convert-available turns %d, taken immediately %.0f%% | deferred %d: %s | fate of deferred neutral: %s | mean delay when same side later %.1f plies'%(c['avail'],100*c['took']/c['avail'],d,{x[6:]:v for x,v in c.items() if x.startswith('defer_')},{x[5:]:v for x,v in c.items() if x.startswith('fate_')},c['delay_sum']/max(1,c['fate_same_side_later'])))
json.dump(ex,open('boss_deferrals.json','w'))
