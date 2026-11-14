"""Behaviour comparison us vs BOSS 3 on baseline-family games. Counts from Java-reconstructed states."""
import json,sys
from collections import defaultdict,Counter
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def phase(t):return 0 if t<40 else 1 if t<100 else 2
def events(g):
    ev=[]
    S=g['states'];A=g['acts']
    for t in range(len(S)-1):
        s,z,a=S[t],S[t+1],A[t];side=t%2;kind,u,tg=a
        e=dict(t=t,side=side,kind=['WAIT','MOVE','SHOOT','CONVERT'][kind],leader=bool(kind and s[4*u+3]))
        if kind==2:
            # actual victim = unit whose hp dropped
            vic=[i for i in range(14) if z[4*i+1]<s[4*i+1]]
            if vic:
                i=vic[0];e.update(victim=i,vowner=s[4*i+2],vleader=bool(s[4*i+3]),dmg=s[4*i+1]-z[4*i+1],kill=z[4*i+1]==0,intended=(i==tg))
            else:e.update(victim=None,dmg=0)
        if kind==3:e.update(towner=s[4*tg+2],thp=s[4*tg+1])
        ev.append(e)
    return ev
rows=[]
for g in G:
    my=1-g['boss_seat'];ev=events(g);S=g['states']
    for who,side in (('us',my),('boss',1-my)):
        r=dict(id=g['id'],seed=g['seed'],seat=side,who=who,res=g['final'][side]-g['final'][1-side],ver=g['version'])
        for p in range(3):
            E=[e for e in ev if e['side']==side and phase(e['t'])==p]
            r['n%d'%p]=len(E)
            r['lmove%d'%p]=sum(e['kind']=='MOVE' and e['leader'] for e in E)
            r['cmove%d'%p]=sum(e['kind']=='MOVE' and not e['leader'] for e in E)
            r['wait%d'%p]=sum(e['kind']=='WAIT' for e in E)
            sh=[e for e in E if e['kind']=='SHOOT']
            r['shoot%d'%p]=len(sh)
            r['sh_neut%d'%p]=sum(e.get('vowner')==2 for e in sh)
            r['sh_lead%d'%p]=sum(e.get('vowner')==1-side and e.get('vleader') for e in sh)
            r['sh_cult%d'%p]=sum(e.get('vowner')==1-side and not e.get('vleader') for e in sh)
            r['sh_own%d'%p]=sum(e.get('vowner')==side for e in sh)
            r['dmg_enemy%d'%p]=sum(e['dmg'] for e in sh if e.get('vowner')==1-side)
            r['kills%d'%p]=sum(bool(e.get('kill')) and e.get('vowner')==1-side for e in sh)
            r['nkill%d'%p]=sum(bool(e.get('kill')) and e.get('vowner')==2 for e in sh)
            r['conv_n%d'%p]=sum(e['kind']=='CONVERT' and e['towner']==2 for e in E)
            r['conv_e%d'%p]=sum(e['kind']=='CONVERT' and e['towner']==1-side for e in E)
        ct=[e['t'] for e in ev if e['side']==side and e['kind']=='CONVERT' and e['towner']==2]
        for k in (1,2,3,4):r['tconv%d'%k]=ct[k-1] if len(ct)>=k else None
        L=side  # leader unit id == side
        for T in (40,100):
            s=S[min(T,len(S)-1)];r['lhp%d'%T]=s[4*L+1] if s[4*L+2]==side else 0;r['cnt%d'%T]=cnt(s,side)
        s=S[-1];r['lalive_end']=int(s[4*L+1]>0)
        rows.append(r)
json.dump(rows,open('behave_rows.json','w'))
def mean(xs):
    xs=[x for x in xs if x is not None];return sum(xs)/len(xs) if xs else float('nan')
keys=['lmove','cmove','wait','shoot','sh_neut','sh_lead','sh_cult','dmg_enemy','kills','nkill','conv_n','conv_e']
print('games',len(G),'distinct maps',len({(g['seed'],g['boss_seat']) for g in G}))
for seat_us in (0,1):
    print('\n=== our seat',seat_us,'games',sum(1 for r in rows if r['who']=='us' and r['seat']==seat_us))
    print('%-10s'%'metric',' '.join('%13s'%('ph%d us/boss'%p) for p in range(3)))
    for k in keys:
        out=[]
        for p in range(3):
            u=mean([r[k+str(p)] for r in rows if r['who']=='us' and r['seat']==seat_us]);b=mean([r[k+str(p)] for r in rows if r['who']=='boss' and r['seat']==1-seat_us])
            out.append('%5.2f /%5.2f'%(u,b))
        print('%-10s'%k,'  '.join(out))
    for k in ['tconv1','tconv2','tconv3','tconv4','lhp40','lhp100','cnt40','cnt100','lalive_end']:
        u=[r[k] for r in rows if r['who']=='us' and r['seat']==seat_us];b=[r[k] for r in rows if r['who']=='boss' and r['seat']==1-seat_us]
        print('%-10s us %.2f (n=%d)  boss %.2f (n=%d)'%(k,mean(u),sum(x is not None for x in u),mean(b),sum(x is not None for x in b)))
