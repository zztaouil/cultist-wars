"""Reconstruct with Java; verify C transitions; cache grouped legal-action features."""
import array, ctypes as C, gzip, hashlib, json, pathlib, random, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parent
import os
os.chdir(ROOT)
assert hashlib.sha256(pathlib.Path('baseline.c').read_bytes()).hexdigest()=='82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d'
t0=time.time(); pathlib.Path('build').mkdir(exist_ok=True)
subprocess.run(['javac','-d','build/java',*map(str,pathlib.Path('java/model').glob('*.java'))],check=True)
subprocess.run(['gcc','-O3','-std=c11','-shared','-fPIC','features.c','-o','build/features.so'],check=True)
lib=C.CDLL(str(ROOT/'build/features.so')); A=C.c_int*62; M=C.c_int*3
lib.setup.argtypes=[C.POINTER(C.c_int)]
lib.extract.argtypes=[C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_float),C.POINTER(C.c_int),C.POINTER(C.c_int)]
lib.verify_step.argtypes=[C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_int)]
games={str(g['id']):g for g in map(json.loads,gzip.open('games.jsonl.gz','rt'))}; inp=[]
for gid,g in games.items():
 if 'TIMEOUT' in g['actions']:continue
 inp.append(f"G {gid} {g['ref'].strip().split('=')[1]} {len(g['actions'])}");inp.extend(g['actions'])
with open('build/recon.txt','w') as out:
 subprocess.run(['java','-cp','build/java','model.Batch'],input='\n'.join(inp)+'\n',text=True,stdout=out,check=True)
experts={'aCat','Noobkins','MSz'}; rng=random.Random(41710886)
X=array.array('f'); orders=array.array('i'); offsets=[0]; labels=[]; splits=[]; records=[]
stats={'games_seen':0,'games_valid':0,'games_rejected':0,'transitions_verified':0,'unmatched_actions':0}; groups={}
def consume(gid,w,states,acts,bad):
 stats['games_seen']+=1;g=games[gid]
 counts=[sum(s[1]>0 and s[2]==side for s in zip(*[iter(states[-1][:56])]*4)) for side in (0,1)] if states else []
 if bad or counts!=g['scores'] or len(states)!=len(g['actions'])+1:stats['games_rejected']+=1;return
 lib.setup((C.c_int*91)(*w))
 for i in range(len(states)-1):
  if not lib.verify_step(A(*states[i]),M(*acts[i]),A(*states[i+1])):stats['games_rejected']+=1;return
 stats['games_valid']+=1;stats['transitions_verified']+=len(states)-1
 # Same wall layout (including its reflection), across all games, stays in one split.
 rows=[w[i:i+13] for i in range(0,91,13)]
 variants=[sum(r,[]) for r in (rows,rows[::-1],[a[::-1] for a in rows],[a[::-1] for a in rows[::-1]])]
 key=hashlib.sha256(bytes(min(variants))).hexdigest();bucket=int(key[:8],16)%100;split=0 if bucket<80 else 1 if bucket<90 else 2
 groups.setdefault(key,set()).add(split)
 players={a['index']:a['pseudo'] for a in g['agents']}
 candidates=[i for i in range(len(states)-1) if players[states[i][56]] in experts]
 rng.shuffle(candidates)
 for i in candidates[:32]:
  buf=(C.c_float*(253*128))();order=(C.c_int*253)();label=C.c_int()
  n=lib.extract(A(*states[i]),M(*acts[i]),buf,order,C.byref(label))
  if label.value<0:stats['unmatched_actions']+=1;continue
  if n<2:continue
  X.extend(buf[:n*128]);orders.extend(order[:n]);offsets.append(offsets[-1]+n);labels.append(label.value);splits.append(split)
  records.append({'game':gid,'turn':i,'player':players[states[i][56]],'map':key})
for line in open('build/recon.txt'):
 if line.startswith('G '):_,gid,ws=line.split();w=list(map(int,ws));states=[];acts=[];bad=False
 elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s);acts.append(a)
 elif line.startswith('X '):bad=True
 elif line.startswith('E'):consume(gid,w,states,acts,bad)
assert all(len(x)==1 for x in groups.values())
assert all(splits.count(i)>100 for i in range(3)), 'Too few held-out positions'
pathlib.Path('cache').mkdir(exist_ok=True)
with open('cache/features.f32','wb') as f:X.tofile(f)
with open('cache/orders.i32','wb') as f:orders.tofile(f)
meta={'offsets':offsets,'labels':labels,'splits':splits,'records':records,'feature_version':1,'baseline_sha256':hashlib.sha256(pathlib.Path('baseline.c').read_bytes()).hexdigest()}
pathlib.Path('cache/meta.json').write_text(json.dumps(meta))
stats.update(positions=len(labels),actions=offsets[-1],split_positions={str(i):splits.count(i) for i in range(3)},feature_MiB=round(len(X)*4/2**20,1),seconds=round(time.time()-t0,1))
pathlib.Path('cache/report.json').write_text(json.dumps(stats,indent=2));print(json.dumps(stats,indent=2),flush=True)
