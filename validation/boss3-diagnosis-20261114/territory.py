"""Do we and BOSS 3 take neutrals in a different order (home vs contested)? Leader advance and territory split."""
import json
from collections import defaultdict,deque
G=json.load(open('boss_games.json'))
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
G=[g for g in G if g['version'] in BASE and g['score_ok']]
def bfs(w,src):
    d=[99]*91;d[src]=0;q=deque([src])
    while q:
        p=q.popleft();x,y=p%13,p//13
        for dx,dy in((1,0),(-1,0),(0,1),(0,-1)):
            nx,ny=x+dx,y+dy
            if 0<=nx<13 and 0<=ny<7 and w[ny*13+nx]=='0' and d[ny*13+nx]==99:d[ny*13+nx]=d[p]+1;q.append(ny*13+nx)
    return d
def mean(x):x=[v for v in x if v is not None];return sum(x)/len(x) if x else float('nan')
for seat_us in (0,1):
    marg=defaultdict(list);adv=defaultdict(list);terr=defaultdict(list);res=defaultdict(list)
    for g in G:
        my=1-g['boss_seat']
        if my!=seat_us:continue
        S=g['states'];w=g['walls'];D0=[bfs(w,S[0][4*p]) for p in (0,1)]
        k=[0,0]
        for t in range(len(S)-1):
            a,b=S[t],S[t+1]
            for i in range(2,14):
                if a[4*i+2]==2 and b[4*i+2] in (0,1) and b[4*i+1]>0:
                    p=b[4*i+2];k[p]+=1;who='us' if p==my else 'boss'
                    marg[(who,min(k[p],6))].append(D0[p][a[4*i]]-D0[1-p][a[4*i]])
        for T in (6,12,20,30,40,60):
            if len(S)<=T:continue
            s=S[T]
            for p in (0,1):
                who='us' if p==my else 'boss'
                if s[4*p+1]>0:adv[(who,T)].append(D0[p][s[4*p]])
            # territory: neutrals closer to each leader's CURRENT position + owned non-leader units
            if s[1]>0 and s[5]>0:
                L=[bfs(w,s[0]),bfs(w,s[4])]
                for p in (0,1):
                    who='us' if p==my else 'boss'
                    near=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2 and L[p][s[4*i]]<L[1-p][s[4*i]])
                    own=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==p)
                    terr[(who,T)].append((own,near))
    print('\n=== our seat',seat_us)
    print('margin of k-th converted neutral (dist from own leader start - dist from opp leader start; <0 home, >0 enemy half)')
    for k in range(1,7):print('  k=%d us %+.2f (n=%d, %%>=0: %.0f)   boss %+.2f (n=%d, %%>=0: %.0f)'%(k,mean(marg[('us',k)]),len(marg[('us',k)]),100*mean([m>=0 for m in marg[('us',k)]]),mean(marg[('boss',k)]),len(marg[('boss',k)]),100*mean([m>=0 for m in marg[('boss',k)]])))
    print('leader BFS distance from own start')
    for T in (6,12,20,30,40,60):print('  T=%2d us %.2f boss %.2f'%(T,mean(adv[('us',T)]),mean(adv[('boss',T)])))
    print('owned cultists / neutrals closer to own leader (both leaders alive)')
    for T in (6,12,20,30,40,60):
        u=terr[('us',T)];b=terr[('boss',T)]
        print('  T=%2d us own %.2f near %.2f sum %.2f | boss own %.2f near %.2f sum %.2f (n=%d)'%(T,mean([x[0] for x in u]),mean([x[1] for x in u]),mean([sum(x) for x in u]),mean([x[0] for x in b]),mean([x[1] for x in b]),mean([sum(x) for x in b]),len(u)))
