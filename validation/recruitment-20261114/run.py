import json,subprocess,sys,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;root=D.parents[1]
def key(w):
 rows=[w[i:i+13] for i in range(0,91,13)]
 return bytes(min(sum(v,[]) for v in (rows,rows[::-1],[r[::-1] for r in rows],[r[::-1] for r in rows[::-1]])))
if not (D/'maps.json').exists():
 used=set()
 for f in (root/'validation').rglob('*maps*.json'):
  try:
   a=json.loads(f.read_text())
   if isinstance(a,list):
    for x in a:
     if isinstance(x,list) and len(x)==3 and isinstance(x[1],list) and len(x[1])==91:used.add(key(x[1]))
  except (ValueError,TypeError):pass
 inp=''.join(f'G {i} {i} 0\n' for i in range(41751000,41752000))
 p=subprocess.run(['/opt/homebrew/opt/openjdk/bin/java','-cp',str(D.parent/'boss3-diagnosis-20261114/model-j11.jar'),'model.Batch'],input=inp,text=True,capture_output=True,check=True)
 maps=[]
 for line in p.stdout.splitlines():
  if line.startswith('G '):_,gid,w=line.split();w=list(map(int,w))
  elif line.startswith('S '):
   s,_=json.loads('['+line[2:]+']');k=key(w)
   if k not in used:maps.append([int(gid),w,s]);used.add(k)
   if len(maps)==96:break
 assert len(maps)==96
 (D/'maps.json').write_text(json.dumps(maps[:32]));(D/'holdout.json').write_text(json.dumps(maps[32:]))
tag=sys.argv[1];spec=sys.argv[2];budget=sys.argv[3] if len(sys.argv)>3 else '20';mapfile=sys.argv[4] if len(sys.argv)>4 else 'maps.json'
jobs=[]
for part in range(4):
 cmd=[sys.executable,'tests/arena_match.py','--a',str(D/'baseline.c'),'--b',spec,'--tag',tag,'--out',str(D),'--maps',str(D/mapfile),'--maps-used','64','--budget-ms',budget,'--shuffle-seed','41751000','--parts','4','--part',str(part)]
 jobs.append(subprocess.Popen(cmd,stdout=subprocess.DEVNULL))
assert all(p.wait()==0 for p in jobs)
subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag',tag,'--out',str(D)],check=True)
