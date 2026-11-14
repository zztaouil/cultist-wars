import json,sys,eng
G={g['id']:g for g in json.load(open('boss_games.json'))}
def board(g,t):
    s=g['states'][t];w=g['walls'];my=1-g['boss_seat'];grid=[['#' if w[y*13+x]=='1' else '.' for x in range(13)] for y in range(7)]
    desc=[]
    for i in range(14):
        if s[4*i+1]>0:
            o=s[4*i+2];ch=('U' if o==my else 'B' if o==1-my else 'n')
            if s[4*i+3]:ch={'U':'@','B':'&'}[ch]
            x,y=s[4*i]%13,s[4*i]//13;grid[y][x]=format(i,'x') ;desc.append('%s%x:%s hp%d (%d,%d)'%('*' if s[4*i+3] else '',i,{'U':'us','B':'boss','@':'us','&':'boss','n':'neut'}[ch],s[4*i+1],x,y))
    cu=sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==my);cb=sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==1-my)
    out=['t=%d mover=%s  count us %d boss %d  (unit ids hex; * leader)'%(t,'us' if t%2==my else 'boss',cu,cb)]
    out+=[''.join(r) for r in grid]
    out.append('  us:   '+', '.join(d for d in desc if ':us' in d));out.append('  boss: '+', '.join(d for d in desc if ':boss' in d));out.append('  neut: '+', '.join(d for d in desc if ':neut' in d))
    return '\n'.join(out)
def moves(g,a,b):
    my=1-g['boss_seat'];return ' | '.join('%d %s:%s'%(t,'U' if t%2==my else 'B',eng.cmd(g['acts'][t])) for t in range(a,min(b,len(g['acts'])-1)))
if __name__=='__main__':
    g=G[sys.argv[1]];print(g['src'],g['version'],'our seat',1-g['boss_seat'],'final',g['final'])
    for t in map(int,sys.argv[2].split(',')):print(board(g,t))
    if len(sys.argv)>3:a,b=map(int,sys.argv[3].split('-'));print(moves(g,a,b))
