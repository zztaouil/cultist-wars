"""Fate of neutrals still neutral at t=T, with BFS distance (walls only) from each leader. Baseline-family games."""
import json
from collections import Counter,defaultdict,deque
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
def mean(x):return sum(x)/len(x) if x else float('nan')
for T in (20,40):
  for seat_us in (0,1):
    fate=Counter();rows=[]
    for g in G:
        my=1-g['boss_seat']
        if my!=seat_us or len(g['states'])<=T:continue
        S=g['states'];s=S[T];w=g['walls']
        ld=[bfs(w,s[4*p]) if s[4*p+1]>0 and s[4*p+2]==p else None for p in (0,1)]
        for i in range(2,14):
            if s[4*i+1]>0 and s[4*i+2]==2:
                f='neutral_end';tt=None
                for t in range(T,len(S)-1):
                    a,b=S[t],S[t+1]
                    if b[4*i+1]==0 and a[4*i+2]==2:f='killed_by_'+('us' if t%2==my else 'boss');tt=t;break
                    if b[4*i+2]!=2:f='conv_'+('us' if b[4*i+2]==my else 'boss');tt=t;break
                du=ld[my][s[4*i]] if ld[my] else None;db=ld[1-my][s[4*i]] if ld[1-my] else None
                fate[f]+=1;rows.append((f,du,db,tt,s[4*my+1],s[4*(1-my)+1]))
    n=sum(1 for g in G if 1-g['boss_seat']==seat_us)
    print('\nT=%d our seat %d games %d  neutrals/game %.2f'%(T,seat_us,n,len(rows)/n),{k:round(v/n,2) for k,v in fate.items()})
    for f in ('conv_us','conv_boss','neutral_end','killed_by_us','killed_by_boss'):
        R=[r for r in rows if r[0]==f and r[1] is not None and r[2] is not None]
        if R:print('  %-14s n=%3d  dist_us %.1f dist_boss %.1f  closer_to_us %.0f%%  t_event %.0f'%(f,len(R),mean([r[1] for r in R]),mean([r[2] for r in R]),100*mean([r[1]<r[2] for r in R]),mean([r[3] for r in R if r[3] is not None]) if any(r[3] for r in R) else -1))
    # by who is closer
    for lab,cond in (('closer us',lambda r:r[1]<r[2]),('equal',lambda r:r[1]==r[2]),('closer boss',lambda r:r[1]>r[2])):
        R=[r for r in rows if r[1] is not None and r[2] is not None and cond(r)]
        c=Counter(r[0] for r in R);print('  %-11s n=%3d'%(lab,len(R)),{k:'%.0f%%'%(100*v/len(R)) for k,v in c.most_common()})
