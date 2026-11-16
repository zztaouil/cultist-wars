import json,re,hashlib,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent
out=[]
for p in sorted(D.glob('judge-*.json')):
 if p.name=='judge-summary.json':continue
 l=json.loads(p.read_text()).get('log','');m=re.search(r'^  (\d) hexagonee',l,re.M);sc=re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',l)
 if not(m and sc):continue
 seat=int(m[1]);ss=list(map(int,sc.groups()));out.append(dict(file=p.name,result='W' if ss[seat]>ss[1-seat] else 'L' if ss[seat]<ss[1-seat] else 'D',score=[ss[seat],ss[1-seat]],elapsed_max=max(map(int,re.findall(r'elapsed_us=(\d+)',l)),default=0),timeout='timeout!' in l))
(D/'judge-summary.json').write_text(json.dumps(out,indent=2))
summary=[]
for tag in ['dev','no_options','holdout']:
 r=subprocess.check_output(['python3','tests/arena_match.py','--summarize','--tag',tag,'--out',str(D)],text=True)
 try:summary.append(json.loads(r))
 except ValueError:pass
(D/'local-summary.json').write_text(json.dumps(summary,indent=2))
manifest={f:{'sha256':hashlib.sha256(Path(f).read_bytes()).hexdigest(),'bytes':Path(f).stat().st_size} for f in ['bot.cpp',str(D/'baseline.cpp'),str(D/'candidate46.cpp'),str(D/'no_options.cpp')]}
(D/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(summary));print('judge',[(x['file'],x['result'],x['elapsed_max']) for x in out])
