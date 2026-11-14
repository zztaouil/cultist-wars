"""Supplementary 24-map equal-time regression pilot; fresh canonical layouts."""
import hashlib,json,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parent
root=R.parents[1]
def key(w):
    rows=[w[i:i+13] for i in range(0,91,13)]
    variants=(rows,rows[::-1],[r[::-1] for r in rows],[r[::-1] for r in rows[::-1]])
    return hashlib.sha256(bytes(min(sum(v,[]) for v in variants))).hexdigest()
path=R/'match-maps.json'
if not path.exists():
    meta=json.loads((R.parent/'nn-41710886/cache/meta.json').read_text());used={x['map'] for x in meta['records']}
    for f in R.parent.rglob('*maps*.json'):
        try:
            data=json.loads(f.read_text())
            if isinstance(data,list):
                for x in data:
                    if isinstance(x,list) and len(x)==3 and isinstance(x[1],list) and len(x[1])==91:used.add(key(x[1]))
        except (ValueError,TypeError):pass
    inp=''.join(f'G {seed} {seed} 0\n' for seed in range(41714000,41715000))
    proc=subprocess.run(['java','-cp',str(R.parent/'nn-41710886/build/java'),'model.Batch'],input=inp,text=True,capture_output=True,check=True)
    maps=[]
    for line in proc.stdout.splitlines():
        if line.startswith('G '):_,gid,w=line.split();w=list(map(int,w))
        elif line.startswith('S '):
            s,_=json.loads('['+line[2:]+']');k=key(w)
            if k not in used:maps.append([int(gid),w,s]);used.add(k)
            if len(maps)==24:break
    assert len(maps)==24;path.write_text(json.dumps(maps))
    (R/'map-manifest.json').write_text(json.dumps([dict(seed=g,map_hash=key(w)) for g,w,s in maps],indent=2))
jobs=[]
for part in range(4):
    cmd=[sys.executable,'tests/arena_match.py','--a',str(R/'baseline.c'),'--b',str(R/'candidate.c'),'--tag','large49','--out',str(R),'--maps',str(path),'--maps-used','24','--budget-ms','49','--shuffle-seed','41713000','--parts','4','--part',str(part)]
    jobs.append(subprocess.Popen(cmd,cwd=root))
assert all(p.wait()==0 for p in jobs)
p=subprocess.run([sys.executable,'tests/arena_match.py','--summarize','--tag','large49','--out',str(R)],cwd=root,text=True,capture_output=True,check=True)
(R/'match-summary.json').write_text(p.stdout);print(p.stdout)
