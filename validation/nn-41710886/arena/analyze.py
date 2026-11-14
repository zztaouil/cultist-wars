import re,json,statistics,subprocess
from pathlib import Path
R=Path(__file__).resolve().parent;summary=[];inputs=[]
for p in sorted(R.glob('game-*.txt')):
 s=p.read_text();gid=int(re.search(r'game (\d+)',s)[1]);seat=int(re.search(r'^  (\d+) hexagonee',s,re.M)[1]);scores=list(map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',s).groups()));st=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(-?\d+)',line)) for line in s.splitlines() if 'stderr: depth=' in line]
 acts=re.findall(r'^  stdout: (.*)$',s,re.M);acts=[' '.join(a.split()[:1 if a.startswith('WAIT') else 4 if ' MOVE ' in a else 3]) for a in acts]
 seed=re.search(r'options: seed=(-?\d+)',s)[1];inputs.append(f'G {gid} {seed} {len(acts)}');inputs.extend(acts)
 r={'game':gid,'seat':seat,'scores':scores,'result':'W' if scores[seat]>scores[1-seat] else 'D' if scores[seat]==scores[1-seat] else 'L','max_elapsed_us':max((x['elapsed_us'] for x in st),default=None),'median_depth':statistics.median(x['depth'] for x in st) if st else None,'rng_zero':sum(x['rng']==0 for x in st),'timeout':bool(re.search(r'not provided|timed out|timeout|invalid action',s,re.I))};summary.append(r)
 if r['result']=='L':
  print('LOSS eval checkpoints',[(2*i+seat,x['advantage'],x['depth']) for i,x in enumerate(st) if i%5==0]);print('First negative',next(((2*i+seat,x) for i,x in enumerate(st) if x['advantage'] < -400),None))
p=subprocess.run(['java','-cp',str(R.parent/'build/java'),'model.Batch'],input='\n'.join(inputs)+'\n',text=True,capture_output=True,check=True);(R/'recon.txt').write_text(p.stdout)
(R/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
