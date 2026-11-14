"""Does early territory / leader advance predict final margin within our boss games? Map-clustered."""
import json,math
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
def feats(g,T):
    my=1-g['boss_seat'];S=g['states'];s=S[T];w=g['walls']
    if not(s[1]>0 and s[5]>0):return None
    L=[bfs(w,s[0]),bfs(w,s[4])];D0=[bfs(w,S[0][0]),bfs(w,S[0][4])]
    t=[]
    for p in (0,1):
        near=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2 and L[p][s[4*i]]<L[1-p][s[4*i]])
        own=sum(1 for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==p);t.append((own,near,D0[p][s[4*p]],s[4*p+1]))
    return dict(cnt=t[my][0]-t[1-my][0],near=t[my][1]-t[1-my][1],terr=sum(t[my][:2])-sum(t[1-my][:2]),adv=t[my][2]-t[1-my][2],lhp=t[my][3]-t[1-my][3])
def corr(x,y):
    n=len(x);mx=sum(x)/n;my=sum(y)/n;sx=math.sqrt(sum((a-mx)**2 for a in x));sy=math.sqrt(sum((b-my)**2 for b in y))
    return sum((a-mx)*(b-my) for a,b in zip(x,y))/(sx*sy) if sx*sy else float('nan')
for T in (12,20,30,40):
    for seat in (0,1,None):
        R=[]
        for g in G:
            my=1-g['boss_seat']
            if seat is not None and my!=seat:continue
            if len(g['states'])<=T:continue
            f=feats(g,T)
            if f:f['margin']=g['final'][my]-g['final'][1-my];R.append(f)
        m=[r['margin'] for r in R]
        print('T=%d seat=%s n=%d  corr(margin, cnt)=%.2f near=%.2f terr=%.2f adv=%.2f lhp=%.2f | mean terr: W %.2f D %.2f L %.2f'%(T,seat,len(R),*[corr([r[k] for r in R],m) for k in ('cnt','near','terr','adv','lhp')],*[ (lambda xs:sum(xs)/len(xs) if xs else float('nan'))([r['terr'] for r in R if c(r['margin'])]) for c in (lambda x:x>0,lambda x:x==0,lambda x:x<0)]))
