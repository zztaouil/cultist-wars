"""Supplementary 64-map equal-time regression pilot; fresh canonical layouts."""
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
    used={json.loads(line)['map'] for line in (R.parent/'imitation-5m-20261114/cache/records.jsonl').open()}
    for f in R.parent.rglob('*maps*.json'):
        try:
            data=json.loads(f.read_text())
            if isinstance(data,list):
                for x in data:
                    if isinstance(x,list) and len(x)==3 and isinstance(x[1],list) and len(x[1])==91:used.add(key(x[1]))
        except (ValueError,TypeError):pass
    inp=''.join(f'G {seed} {seed} 0\n' for seed in range(41734000,41735000))
    proc=subprocess.run(['java','-cp',str(R.parent/'nn-41710886/build/java'),'model.Batch'],input=inp,text=True,capture_output=True,check=True)
    maps=[]
    for line in proc.stdout.splitlines():
        if line.startswith('G '):_,gid,w=line.split();w=list(map(int,w))
        elif line.startswith('S '):
            s,_=json.loads('['+line[2:]+']');k=key(w)
            if k not in used:maps.append([int(gid),w,s]);used.add(k)
            if len(maps)==64:break
    assert len(maps)==64;path.write_text(json.dumps(maps))
    (R/'map-manifest.json').write_text(json.dumps([dict(seed=g,map_hash=key(w)) for g,w,s in maps],indent=2))

print('Frozen',len(json.loads(path.read_text())),'fresh paired-match maps',flush=True)
