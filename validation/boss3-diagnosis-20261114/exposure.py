"""Leader exposure after own action: can enemy cultists hit our leader next turn (max dmg, #shooters)? Leader move direction."""
import json,eng
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
acc=defaultdict(lambda:defaultdict(float))
for g in G:
    my=1-g['boss_seat'];eng.setup(g['walls']);S=g['states'];D0=[bfs(g['walls'],S[0][0]),bfs(g['walls'],S[0][4])]
    for t in range(len(S)-1):
        side=t%2;who='us' if side==my else 'boss';ph=min(t//20,4);s=S[t];n=S[t+1];a=g['acts'][t]
        if s[4*side+1]<=0:continue
        k=(who,ph);A=acc[k];A['turns']+=1
        L=eng.legal(n);hits=[x[4] for x in L if x[0]==2 and x[3]==side]
        A['exposed']+=bool(hits);A['maxdmg']+=max(hits) if hits else 0;A['exp3']+=bool(hits) and max(hits)>=3
        if a[0]==1 and a[1]==side:
            A['lmoves']+=1;d=D0[side][n[4*side]]-D0[side][s[4*side]];A['fwd']+=d>0;A['back']+=d<0
            A['lm_exposed']+=bool(hits);A['lm_maxdmg']+=max(hits) if hits else 0
            # distance to nearest neutral before/after
            ne=[i for i in range(2,14) if s[4*i+1]>0 and s[4*i+2]==2]
            if ne:
                b0=bfs(g['walls'],s[4*side]);b1=bfs(g['walls'],n[4*side]);A['lm_n']+=1;A['toward_neut']+=min(b1[s[4*i]] for i in ne)<min(b0[s[4*i]] for i in ne)
            # was leader shootable BEFORE moving (retreat from fire)?
            Lb=eng.legal(s[:56]+[1-side]+s[57:]);hb=[x[4] for x in Lb if x[0]==2 and x[3]==side];A['lm_from_fire']+=bool(hb)
for who in ('us','boss'):
    print(who)
    for ph in range(5):
        A=acc[(who,ph)]
        if A['turns']:print('  t%3d-%3d leader exposed after own action %.0f%% (dmg>=3: %.0f%%, mean maxdmg %.2f) | leader moves %.1f/game fwd %.0f%% back %.0f%% toward-nearest-neutral %.0f%% ends exposed %.0f%% started under fire %.0f%%'%(ph*20,ph*20+19,100*A['exposed']/A['turns'],100*A['exp3']/A['turns'],A['maxdmg']/A['turns'],A['lmoves']/len(G),100*A['fwd']/max(1,A['lmoves']),100*A['back']/max(1,A['lmoves']),100*A['toward_neut']/max(1,A['lm_n']),100*A['lm_exposed']/max(1,A['lmoves']),100*A['lm_from_fire']/max(1,A['lmoves'])))
