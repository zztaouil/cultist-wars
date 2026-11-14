import json
from collections import deque
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
for seat in (0,1):
  for res,c in (('W/D',lambda x:x>=0),('L',lambda x:x<0)):
    print('seat',seat,res)
    for T in (12,20,30,40,50,60):
        R=[]
        for g in G:
            my=1-g['boss_seat'];S=g['states']
            if my!=seat or not c(g['final'][my]-g['final'][1-my]) or len(S)<=T:continue
            s=S[T];w=g['walls'];D0=[bfs(w,S[0][0]),bfs(w,S[0][4])]
            alive=[s[4*p+1]>0 for p in (0,1)]
            L=[bfs(w,s[4*p]) if alive[p] else [99]*91 for p in (0,1)]
            o=[]
            for p in (my,1-my):
                near=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2 and L[p][s[4*i]]<L[1-p][s[4*i]])
                own=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==p)
                chp=sum(s[4*i+1] for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==p)
                # nearest neutral distance for leader
                nn=min([L[p][s[4*i]] for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2] or [0])
                o+= [own,near,D0[p][s[4*p]] if alive[p] else 0,s[4*p+1],chp/own if own else 0,nn if alive[p] else 0]
            neut=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2)
            R.append(o+[neut])
        if R:
            m=[mean([r[k] for r in R]) for k in range(13)]
            print('  T=%2d n=%2d | us own %.2f near %.2f adv %.1f Lhp %.1f chp %.1f dNearestNeut %.1f | boss own %.2f near %.2f adv %.1f Lhp %.1f chp %.1f dNN %.1f | neutrals %.2f'%(T,len(R),*m))
