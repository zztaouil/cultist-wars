"""Offline manipulation pre-check for the leader-pressure hypothesis (NOT a strength test).
Same positions (our decisions t13-45, exact RNG, fresh caches, 49 ms). Compare lhp=0 (frozen 41711135 eval) vs extra points per leader HP (both sides)."""
import json,eng
from collections import Counter
G={g['id']:g for g in json.load(open('boss_games.json'))}
P={}
for l in open('horizon.jsonl'):
    o=json.loads(l)
    if not o.get('incomplete'):P[(o['id'],o['t'])]=o
rows=[];cur=None
for (gid,t),o in sorted(P.items()):
    g=G[gid]
    if cur!=gid:eng.setup(g['walls']);cur=gid
    s=g['states'][t];my=o['seat'];opp=1-my
    L=eng.legal(s);ls={x[:3]:x[4] for x in L if x[0]==2 and x[3]==opp}
    r=dict(id=gid,t=t,leader_shot_avail=bool(ls),opp_leader_alive=s[4*opp+1]>0)
    for k in (0,0,40,80):
        ch,v,d,n=eng.timed(s,49,k);key='lhp%d'%k+('b' if k==0 and 'lhp0' in r else '');r[key]=ch;r[key+'_hit']=ch in ls;r[key+'_d']=d
    rows.append(r)
json.dump(rows,open('lhp_probe.json','w'))
n=len(rows);A=[r for r in rows if r['leader_shot_avail']]
print('positions',n,'with a shot on boss leader available',len(A))
print('run-to-run noise (lhp0 vs lhp0 repeat) choice differs: %.1f%%'%(100*sum(r['lhp0']!=r['lhp0b'] for r in rows)/n))
for k in (40,80):print('lhp%d vs lhp0 choice differs: %.1f%% ; mean depth %.2f vs %.2f'%(k,100*sum(r['lhp0']!=r['lhp%d'%k] for r in rows)/n,sum(r['lhp%d_d'%k] for r in rows)/n,sum(r['lhp0_d'] for r in rows)/n))
for key in ('lhp0','lhp0b','lhp40','lhp80'):print(' %s: takes leader shot when available %.1f%% (%d/%d)'%(key,100*sum(r[key+'_hit'] for r in A)/len(A),sum(r[key+'_hit'] for r in A),len(A)))
