import json,random,sys,collections as C
import harness as h
from load import *
H=json.load(open('data/handles.json'));AG={v[1]:k for k,v in H.items()}
want={'aCat':900,'Noobkins':500,'MSz':500,'hexagonee':300}
part,nparts=int(sys.argv[1]),int(sys.argv[2])
random.seed(7)
pos=C.defaultdict(list)
for g in games():
  ag=sorted(g['meta']['agents'],key=lambda a:a['index']);sc=g['meta']['scores']
  for side in (0,1):
    n=AG.get(ag[side]['agentId'])
    if n not in want: continue
    r=(sc[side]>sc[1-side])-(sc[side]<sc[1-side])
    for k in range(0,len(g['states'])-1):
      if g['states'][k][56]==side: pos[n].append((g['id'],k,r,g['walls'],g['states'][k],g['actions'][k]))
sample=[]
for n,c in want.items():
  for x in random.sample(pos[n],c): sample.append((n,)+x)
sample=sample[part::nparts]
lib=h.build('bot.c',f'bot{part}')
out=open(f'data/agree{part}.jsonl','w')
for n,gid,k,r,w,s,a in sample:
  h.setup(lib,w);res=h.run(lib,s)
  out.write(json.dumps(dict(player=n,game=gid,turn=k,res=r,theirs=a,ours=res['action'],depth=res['depth'],value=res['value'],state=s,walls=w))+'\n');out.flush()
