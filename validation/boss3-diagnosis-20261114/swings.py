import json
from collections import defaultdict
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def mean(x):return sum(x)/len(x) if x else float('nan')
rows=[]
for g in G:
    my=1-g['boss_seat'];S=g['states'];d=[cnt(s,my)-cnt(s,1-my) for s in S]
    def at(T):return d[min(T,len(d)-1)]
    r=dict(id=g['id'],seat=my,ver=g['version'],src=g['src'],seed=g['seed'],d20=at(20),d40=at(40),d70=at(70),d100=at(100),d130=at(130),fin=d[-1],n=len(S)-1)
    r['perm']=next((t for t in range(len(d)) if max(d[t:])<0),None) if d[-1]<0 else None
    r['maxlead']=max(d);r['tmax']=d.index(max(d))
    rows.append(r)
json.dump(rows,open('swing_rows.json','w'),indent=0)
for seat in (0,1):
    for res,c in (('W',lambda x:x>0),('D',lambda x:x==0),('L',lambda x:x<0)):
        R=[r for r in rows if r['seat']==seat and c(r['fin'])]
        if R:print('seat',seat,res,'n=%2d'%len(R),' '.join('%s %+.2f'%(k,mean([r[k] for r in R])) for k in ('d20','d40','d70','d100','d130','fin')),'| maxlead %.2f'%mean([r['maxlead'] for r in R]),'perm-deficit median',sorted([r['perm'] for r in R if r['perm'] is not None])[len(R)//2] if res=='L' else '')
L=[r for r in rows if r['fin']<0]
print('losses',len(L),'led by >=1 at some point:',sum(r['maxlead']>=1 for r in L),' led at T=40:',sum(r['d40']>0 for r in L),' led at T=100:',sum(r['d100']>0 for r in L),' level at 100:',sum(r['d100']==0 for r in L))
print('perm deficit start histogram (losses):',{b:sum(1 for r in L if r['perm'] is not None and b<=r['perm']<b+20) for b in range(0,150,20)})
W=[r for r in rows if r['fin']>0];print('wins',len(W),'behind at T=40:',sum(r['d40']<0 for r in W),'behind at 100:',sum(r['d100']<0 for r in W))
