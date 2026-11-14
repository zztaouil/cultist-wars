import json,sys,eng
from collections import deque
G={g['id']:g for g in json.load(open('boss_games.json'))}
def bfs(w,src):
    d=[99]*91;d[src]=0;q=deque([src])
    while q:
        p=q.popleft();x,y=p%13,p//13
        for dx,dy in((1,0),(-1,0),(0,1),(0,-1)):
            nx,ny=x+dx,y+dy
            if 0<=nx<13 and 0<=ny<7 and w[ny*13+nx]=='0' and d[ny*13+nx]==99:d[ny*13+nx]=d[p]+1;q.append(ny*13+nx)
    return d
def tl(g):
    my=1-g['boss_seat'];S=g['states'];w=g['walls'];out=[]
    for t in range(len(S)-1):
        a,b=S[t],S[t+1];who='U' if t%2==my else 'B'
        for i in range(14):
            if a[4*i+1]>0 and b[4*i+1]==0:out.append('%d %s kills %s%d'%(t,who,{my:'u',1-my:'b',2:'n'}[a[4*i+2]],i))
            elif a[4*i+2]!=b[4*i+2] and b[4*i+1]>0:
                Lu=bfs(w,a[4*my])[a[4*i]] if a[4*my+1]>0 else 99;Lb=bfs(w,a[4*(1-my)])[a[4*i]] if a[4*(1-my)+1]>0 else 99
                out.append('%d %s conv %s%d(hp%d)'%(t,who,'n' if a[4*i+2]==2 else 'ENEMY',i,a[4*i+1]))
    return out
if __name__=='__main__':
    R=[g for g in G.values() if g['me'] and g['version'] in ('41709358','41710268','41711135','cd28(=41710268)') and g['boss_seat']==int(sys.argv[1])]
    for g in R:
        my=1-g['boss_seat'];m=g['final'][my]-g['final'][1-my]
        print(g['id'],g['version'],g['src'].split('/')[0],'seed',g['seed'][-6:],'final us-boss %d-%d'%(g['final'][my],g['final'][1-my]));print('   ',' | '.join(tl(g)[:28]))
