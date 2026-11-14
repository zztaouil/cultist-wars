import json,glob,re,subprocess,sys
games=sorted(glob.glob('data/games/*.json'))
inp=[];meta={}
for fn in games:
  g=json.load(open(fn));m=re.search(r'seed=(-?\d+)',g['refereeInput'] or '')
  if not m: continue
  acts=[]
  for f in g['frames']:
    if 'timeout' in f['summary'].lower() or 'timed out' in (f['gi'] or '').lower(): break
    s=f['stdout'].strip()
    if s: acts.append(s.split('\n')[0].split(';')[0].strip())
  inp.append(f"G {g['gameId']} {m[1]} {len(acts)}");inp+=acts
p=subprocess.run(['java','-cp','java/out','model.Batch'],input='\n'.join(inp)+'\n',text=True,capture_output=True)
out=open('data/recon.txt','w');out.write(p.stdout);print(p.stderr[-500:])
print(p.stdout.count('\nE'),p.stdout.count('\nX '))
