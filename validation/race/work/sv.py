import numpy as np,random,sys,time,json
import harness as h
A=np.load('X.npy');S=np.load('S.npy');W=np.load('W.npy')
part,nparts,depth,N=map(int,sys.argv[1:5])
lib=h.build('bot.c',f'sv{part}')
idx=[i for i in range(len(A)) if A[i,3]!=0.5]
random.Random(5).shuffle(idx);idx=sorted(idx[:N])[part::nparts]
out=open(f'sv_d{depth}_{part}.jsonl','w');t=time.time();last=-1
for i in idx:
  gi=int(A[i,0])
  if gi!=last: h.setup(lib,list(map(int,W[gi-1])));last=gi
  r=h.run(lib,list(map(int,S[i])),depth)
  out.write(json.dumps([i,r['value'],r['nodes']])+'\n')
print(part,len(idx),time.time()-t)
