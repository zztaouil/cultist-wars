from pathlib import Path
import subprocess,json
D=Path(__file__).resolve().parent
r=subprocess.run(['/opt/homebrew/opt/openjdk/bin/java','-cp',str(D.parent/'boss3-diagnosis-20261114/model-j11.jar'),'model.Batch'],input=''.join(f'G {i} {i} 0\n' for i in range(41764000,41764032)),text=True,capture_output=True,check=True);maps=[]
for line in r.stdout.splitlines():
 if line.startswith('G '):_,gid,w=line.split();w=list(map(int,w))
 elif line.startswith('S '):s,_=json.loads('['+line[2:]+']');maps.append([int(gid),w,s])
(D/'holdout.json').write_text(json.dumps(maps))
jobs=[]
for part in range(4):
 jobs.append(subprocess.Popen(['python3','tests/arena_match.py','--a',str(D/'baseline.c'),'--b',str(D/'candidate.c'),'--tag','holdout48','--out',str(D),'--maps',str(D/'holdout.json'),'--maps-used','32','--budget-ms','48','--parts','4','--part',str(part)],stdout=subprocess.DEVNULL,stderr=open(D/f'holdout-{part}.log','w')))
assert all(p.wait()==0 for p in jobs)
subprocess.run(['python3','tests/arena_match.py','--summarize','--tag','holdout48','--out',str(D)],check=True)
