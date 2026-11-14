import json,sys,eng
G={g['id']:g for g in json.load(open('boss_games.json'))}
g=G[sys.argv[1]];eng.setup(g['walls']);my=1-g['boss_seat']
for t in map(int,sys.argv[2].split(',')):
    s=g['states'][t];a=tuple(g['acts'][t]);out=['t=%d actual %s'%(t,eng.cmd(a))]
    for ms in (49,500,3000):
        ch,v,d,n=eng.timed(s,ms);out.append('%dms: %s v=%d d=%d'%(ms,eng.cmd(ch),v,d))
    print(' | '.join(out),flush=True)
    for depth in (6,8):
        cv=eng.child_values(s,depth,0,40000)
        if cv:
            top=sorted(cv.items(),key=lambda x:-x[1])[:4];print('    d%d: actual=%s  top: %s'%(depth,cv.get(a),', '.join('%s %d'%(eng.cmd(k),v) for k,v in top)),flush=True)
