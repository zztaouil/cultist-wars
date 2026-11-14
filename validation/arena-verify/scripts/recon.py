import json,gzip,subprocess,sys
G=[json.loads(l) for l in gzip.open(sys.argv[1],'rt')]
inp=[]
for g in G:
  seed=g['ref'].strip().split('=')[1]
  acts=[]
  for a in g['actions']:
    if a=='TIMEOUT': break
    acts.append(a)
  inp.append(f"G {g['id']} {seed} {len(acts)}");inp+=acts
p=subprocess.run(['java','-cp','java/out','model.Batch'],input='\n'.join(inp)+'\n',text=True,capture_output=True)
open('recon.txt','w').write(p.stdout);print(p.stderr[-500:]);print(p.stdout.count('\nE'),p.stdout.count('\nX '))
