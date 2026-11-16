"""jsonl -> npz. Arrays: S(62 int), V (pl0 search value), DEP, T(8 terms), THR, RES (pl0 result), DIFF, MAP, GAME."""
import json,glob,numpy as np,sys
S=[];V=[];DEP=[];T=[];THR=[];RES=[];DIFF=[];MAP=[];GAME=[];PLY=[];g=0
for f in sorted(sum([glob.glob(p) for p in (sys.argv[2:] or ['data/*.jsonl'])],[])):
  for line in open(f):
    try:G=json.loads(line)
    except Exception:continue
    for k,(s,v,d,t,th) in enumerate(G['rows']):
      S.append(s);V.append(v);DEP.append(d);T.append(t);THR.append(th);RES.append(G['res']);DIFF.append(G['diff']);MAP.append(G['map']);GAME.append(g);PLY.append(k)
    g+=1
np.savez_compressed(sys.argv[1] if len(sys.argv)>1 else 'data.npz',S=np.array(S,np.int32),V=np.array(V,np.int32),DEP=np.array(DEP,np.int8),T=np.array(T,np.float32),THR=np.array(THR,np.int8),RES=np.array(RES,np.int8),DIFF=np.array(DIFF,np.int8),MAP=np.array(MAP,np.int64),GAME=np.array(GAME,np.int32),PLY=np.array(PLY,np.int16))
print('games',g,'positions',len(S))
