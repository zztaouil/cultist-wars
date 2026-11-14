"""How often is a shot on BOSS's leader within eps of the engine-best move (depth-6 child values, frozen eval)?
Offline pre-check for a root tie-break experiment. Our decision points t13-45 where such a shot is legal."""
import json,eng
G={g['id']:g for g in json.load(open('boss_games.json'))}
R=[r for r in json.load(open('lhp_probe.json')) if r['leader_shot_avail']]
out=[];cur=None
for r in R:
    g=G[r['id']]
    if cur!=r['id']:eng.setup(g['walls']);cur=r['id']
    s=g['states'][r['t']];my=1-g['boss_seat'];opp=1-my
    L=eng.legal(s);ls={x[:3]:x[4] for x in L if x[0]==2 and x[3]==opp}
    cv=eng.child_values(s,6,0,8000)
    if not cv:continue
    best=max(cv.values());bl=max(cv[a] for a in ls if a in cv);a=tuple(g['acts'][r['t']])
    out.append(dict(id=r['id'],t=r['t'],gap=best-bl,took=a in ls,dmg=max(ls.values()),lhp=s[4*opp+1],neutrals=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2)))
json.dump(out,open('tiebreak_probe.json','w'))
n=len(out);print('positions',n,'in-game took leader shot',sum(o['took'] for o in out))
for eps in (0,25,50,100,200):print(' leader shot within %3d of depth-6 best: %.0f%% (%d)'%(eps,100*sum(o['gap']<=eps for o in out)/n,sum(o['gap']<=eps for o in out)))
nt=[o for o in out if not o['took']];print(' among declined (%d): within 50: %d, within 100: %d ; median gap %d'%(len(nt),sum(o['gap']<=50 for o in nt),sum(o['gap']<=100 for o in nt),sorted(o['gap'] for o in nt)[len(nt)//2]))
