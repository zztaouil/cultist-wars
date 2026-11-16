import json,subprocess,sys,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1]
# New seeds; exclude canonical wall layouts already used in any saved map list.
def key(w):
 rows=[w[i:i+13] for i in range(0,91,13)]
 return min(tuple(sum(v,[])) for v in [rows,rows[::-1],[r[::-1] for r in rows],[r[::-1] for r in rows[::-1]]])
seen=set()
for p in (ROOT/'validation').rglob('*maps*.json'):
 try:
  a=json.loads(p.read_text())
  if isinstance(a,list):
   for x in a:
    if isinstance(x,list) and len(x)==3 and isinstance(x[1],list) and len(x[1])==91:seen.add(key(x[1]))
 except (ValueError,TypeError):pass
r=subprocess.run(['java','-cp',str(D.parent/'boss3-diagnosis-20261114/model-j11.jar'),'model.Batch'],input=''.join(f'G {i} {i} 0\n' for i in range(69114000,69116000)),capture_output=True,text=True,check=True)
maps=[]
for line in r.stdout.splitlines():
 if line.startswith('G '):_,gid,w=line.split();w=list(map(int,w))
 elif line.startswith('S '):
  s,_=json.loads('['+line[2:]+']');k=key(w)
  if k not in seen:maps.append([int(gid),w,s]);seen.add(k)
  if len(maps)==64:break
assert len(maps)==64
mp=D/'holdout-maps.json';mp.write_text(json.dumps(maps))
jobs=[]
for i in range(4):
 jobs.append(subprocess.Popen([sys.executable,'tests/arena_match.py','--a',str(D/'live.c'),'--b',str(D/'cache.c')+':Q_PLIES=3','--tag','q3holdout','--out',str(D),'--maps',str(mp),'--maps-used','64','--budget-ms','49','--part',str(i),'--parts','4']))
assert all(j.wait()==0 for j in jobs)
