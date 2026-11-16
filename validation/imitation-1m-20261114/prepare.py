"""Verify original transitions; deduplicate visible states; augment train only."""
import argparse,array,ctypes as C,gzip,hashlib,json,random,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent/'nn-41710886'
TARGET=1_000_000

def reflect_pos(p,t):
 return (12-p%13 if t&1 else p%13)+13*(6-p//13 if t&2 else p//13)
def reflect(w,s,a,t):
 ww=[0]*91
 for p,v in enumerate(w):ww[reflect_pos(p,t)]=v
 ss=s.copy()
 for j in range(14):ss[j*4]=reflect_pos(s[j*4],t) if s[j*4+1]>0 else 0
 aa=a.copy()
 if aa[0]==1:aa[2]=reflect_pos(aa[2],t)
 return ww,ss,aa

def main():
 p=argparse.ArgumentParser();p.add_argument('--corpus',type=Path,default=ROOT/'games.jsonl.gz');p.add_argument('--cache',type=Path,default=ROOT/'cache');p.add_argument('--max-games',type=int);args=p.parse_args()
 start=time.monotonic();build=ROOT/'build';build.mkdir(exist_ok=True)
 subprocess.run(['javac','-d',str(build/'java'),*map(str,(BASE/'java/model').glob('*.java'))],check=True)
 # Same frozen engine/features; expose shot victim to reject asymmetric ray tie cases.
 (build/'features.c').write_text('#include "'+str(BASE/'features.c')+'"\nint shot_victim(const int*p,const int*a){State s=unpack(p);return victim(&s,(Action){a[0],a[1],a[2],0});}\n')
 subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(build/'features.c'),'-o',str(build/'features.so')],check=True)
 lib=C.CDLL(str(build/'features.so'));A=C.c_int*62;M=C.c_int*3;W=C.c_int*91
 lib.setup.argtypes=[C.POINTER(C.c_int)]
 lib.extract.argtypes=[C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_float),C.POINTER(C.c_int),C.POINTER(C.c_int)]
 lib.verify_step.argtypes=[C.POINTER(C.c_int)]*3
 lib.shot_victim.argtypes=[C.POINTER(C.c_int)]*2
 games={str(g['id']):g for g in map(json.loads,gzip.open(args.corpus,'rt'))}
 experts={r['nickname'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
 gids=sorted(games);random.Random(17).shuffle(gids)
 if args.max_games:gids=gids[:args.max_games]
 with (build/'input.txt').open('w') as f:
  for gid in gids:
   g=games[gid]
   if 'TIMEOUT' in g['actions'] or not g['actions']:continue
   f.write(f"G {gid} {g['ref'].strip().split('=')[1]} {len(g['actions'])}\n"+'\n'.join(g['actions'])+'\n')
 with (build/'input.txt').open() as inp,(build/'recon.txt').open('w') as out:
  subprocess.run(['java','-cp',str(build/'java'),'model.Batch'],stdin=inp,stdout=out,check=True)
 cache=args.cache;cache.mkdir(exist_ok=False)
 xf=(cache/'features.f32').open('wb');rf=(cache/'records.jsonl').open('w')
 offsets=[0];labels=[];splits=[];seen=set();counts=[0,0,0];players={};variants=[0]*4
 stats=dict(corpus_games=len(games),timeout_or_empty_games_excluded=sum('TIMEOUT' in g['actions'] or not g['actions'] for g in games.values()),games_seen=0,games_valid=0,games_rejected=0,transitions_verified=0,original_positions=0,duplicates=0,reflection_ray_rejected=0,unmatched=0,forced=0)
 buf=(C.c_float*(253*128))();order=(C.c_int*253)();label=C.c_int()
 def consume(gid,w,states,acts,bad):
  stats['games_seen']+=1;g=games[gid]
  final=[sum(states[-1][j*4+1]>0 and states[-1][j*4+2]==side for j in range(14)) for side in (0,1)] if states else []
  if bad or final!=g['scores'] or len(states)!=len(g['actions'])+1:stats['games_rejected']+=1;return
  lib.setup(W(*w))
  for i in range(len(states)-1):
   if not lib.verify_step(A(*states[i]),M(*acts[i]),A(*states[i+1])):stats['games_rejected']+=1;return
  stats['games_valid']+=1;stats['transitions_verified']+=len(states)-1
  key=hashlib.sha256(bytes(min(reflect(w,states[0],acts[0],t)[0] for t in range(4)))).hexdigest()
  bucket=int(key[:8],16)%100;split=0 if bucket<80 else 1 if bucket<90 else 2
  names={a['index']:a['pseudo'] for a in g['agents']}
  candidates=[i for i,s in enumerate(states[:-1]) if names[s[56]] in experts]
  victims={i:lib.shot_victim(A(*states[i]),M(*acts[i])) for i in candidates}
  for t in (range(4) if split==0 else [0]):
   ww=reflect(w,states[0],acts[0],t)[0];lib.setup(W(*ww))
   for i in candidates:
    if split==0 and counts[0]>=TARGET:break
    s=states[i];a=acts[i]
    _,ss,aa=reflect(w,s,a,t)
    if a[0]==2 and lib.shot_victim(A(*ss),M(*aa))!=victims[i]:stats['reflection_ray_rejected']+=1;continue
    n=lib.extract(A(*ss),M(*aa),buf,order,C.byref(label))
    if label.value<0:stats['unmatched']+=1;continue
    if n<2:stats['forced']+=1;continue
    # Observable state includes fixed IDs, seat, remaining turns; excludes hidden RNG.
    observable=ss[:58]
    for j in range(14):
     if observable[j*4+1]==0:observable[j*4:j*4+4]=[0]*4
    digest=hashlib.blake2b(bytes(ww)+array.array('i',observable).tobytes(),digest_size=16).digest()
    if digest in seen:stats['duplicates']+=1;continue
    seen.add(digest);xf.write(C.string_at(buf,n*128*4));offsets.append(offsets[-1]+n);labels.append(label.value);splits.append(split);counts[split]+=1;variants[t]+=1
    stats['original_positions']+=int(t==0);players[names[s[56]]]=players.get(names[s[56]],0)+1
    rf.write(json.dumps(dict(game=gid,turn=i,player=names[s[56]],map=key,reflection=t,state_sha=digest.hex()))+'\n')
  if stats['games_seen']%200==0:print(json.dumps(dict(**stats,split_positions=counts,seconds=round(time.monotonic()-start))),flush=True)
 for line in (build/'recon.txt').open():
  if line.startswith('G '):_,gid,ws=line.split();w=list(map(int,ws));states=[];acts=[];bad=False
  elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s);acts.append(a)
  elif line.startswith('X '):bad=True
  elif line.startswith('E'):consume(gid,w,states,acts,bad)
 xf.close();rf.close()
 meta=dict(offsets=offsets,labels=labels,splits=splits,feature_version=1,baseline_sha256=hashlib.sha256((BASE/'baseline.c').read_bytes()).hexdigest())
 (cache/'meta.json').write_text(json.dumps(meta))
 stats.update(positions=len(labels),split_positions=counts,reflection_counts=variants,player_positions=players,feature_bytes=offsets[-1]*128*4,seconds=round(time.monotonic()-start),target_train_positions=TARGET,target_reached=counts[0]>=TARGET)
 (cache/'report.json').write_text(json.dumps(stats,indent=2));print(json.dumps(stats),flush=True)
 assert all(n>100 for n in counts)
if __name__=='__main__':main()
