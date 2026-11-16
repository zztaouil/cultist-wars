import json,re,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent
inp=[];games=[]
for p in sorted(D.glob('game-*.txt')):
 text=p.read_text();seed=re.search(r'seed=(-?\d+)',text)[1]
 acts=re.findall(r'^  stdout: (.+)',text,re.M)
 inp.append(f'G {p.stem} {seed} {len(acts)}');inp+=acts
 games.append(dict(id=p.stem,seat=int(re.search(r'^  (\d) hexagonee',text,re.M)[1]),actions=acts,stats=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(-?\d+)',line)) for line in text.splitlines() if 'stderr: depth=' in line]))
r=subprocess.run(['/opt/homebrew/opt/openjdk/bin/java','-cp',str(D.parent/'boss3-diagnosis-20261114/model-j11.jar'),'model.Batch'],input='\n'.join(inp)+'\n',capture_output=True,text=True,check=True)
by={g['id']:g for g in games}
for line in r.stdout.splitlines():
 if line.startswith('G '):
  _,gid,w=line.split();cur=by[gid];cur.update(walls=[int(c) for c in w],states=[],acts=[])
 elif line.startswith('S '):
  s,a=json.loads('['+line[2:]+']');cur['states'].append(s);cur['acts'].append(a)
 elif line.startswith('X '):raise RuntimeError(line)
for g in games:
 text=(D/(g['id']+'.txt')).read_text()
 sc=re.search(r'scores: 0=(-?[\d.]+) 1=(-?[\d.]+)',text);g['scores']=[int(float(x)) for x in sc.groups()]
 s=g['states'][-1];g['final']=[sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in range(2)]
 g['score_ok']=g['final']==g['scores'];g['opp']=re.search(r'^  '+str(1-g['seat'])+r' (.+?) \(',text,re.M)[1]
(D/'reconstructed.json').write_text(json.dumps(games))
print('reconstructed',len(games),'scores match',sum(g['score_ok'] for g in games))
