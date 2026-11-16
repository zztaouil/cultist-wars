import gzip,time
from experiment import *
candidate();lib=library('collector',sample=True);teacher=library('teacher')
corpus=REPO/'validation/imitation-1m-20261114'
games={str(g['id']):g for g in map(json.loads,gzip.open(corpus/'games.jsonl.gz','rt'))}
X=[];Y=[];B=[];S=[];O=[];records=[];accepted=0;timeouts=0;seen=set();start=time.monotonic()
def consume(gid,w,states,acts,bad):
    global accepted,timeouts
    if bad or not states:return
    g=games[gid];final=[sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1) for s in [states[-1]]]
    if len(states)!=len(g['actions'])+1 or final!=g['scores']:return
    h.setup(lib,w)
    for s,a,n in zip(states,acts,states[1:]):
        got=h.step(lib,s,a)
        if got[:61]!=n[:61]:return
    if all(final) and states[-1][57]>0:return
    accepted+=1;key=map_key(w);bucket=int(key[:8],16)%100;split=0 if bucket<80 else 1 if bucket<90 else 2
    outcome=(final[0]>final[1])-(final[0]<final[1])
    for t in sorted(set([len(acts)//3,2*len(acts)//3])):
        root=states[t];h.setup(lib,w);lib.reset_samples();h.run(lib,root)
        candidates=[(root,'replay')]
        for i in range(lib.sample_count()):
            a=h.Array();lib.get_sample(i,a);candidates.append((list(a),'search'))
        for s,origin in candidates:
            # Numeric RNG serialized signed in C; preserve its 32-bit pattern.
            identity=(key,tuple(s[:61]))
            if identity in seen:continue
            seen.add(identity);h.setup(teacher,w)
            if not teacher.quiet(h.Array(*s)):continue
            out=C.c_int()
            if not teacher.label(h.Array(*s),C.byref(out)):timeouts+=1;continue
            if abs(out.value)>=500000:continue
            f=(C.c_float*16)();teacher.get_features(h.Array(*s),f)
            base=teacher.static_eval(h.Array(*s));sign=1 if s[56]==0 else -1
            X.append(list(f));Y.append(sign*out.value);B.append(sign*base);S.append(split)
            # Only observed replay states inherit verified game outcomes.
            O.append(outcome if origin=='replay' else float('nan'))
            records.append(dict(game=gid,map=key,split=split,origin=origin,state=s,walls=w))
    if accepted%25==0:print(json.dumps(dict(games=accepted,positions=len(X),seconds=time.monotonic()-start)),flush=True)
with (corpus/'build/recon.txt').open() as stream:
    for line in stream:
        if line.startswith('G '):_,gid,ws=line.split();w=list(map(int,ws));states=[];acts=[];bad=False
        elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');states.append(s);acts.append(a)
        elif line.startswith('X '):bad=True
        elif line.startswith('E'):
            consume(gid,w,states,acts,bad)
            if accepted>=200:break
np.savez(ROOT/'data.npz',x=np.array(X,dtype='f4'),teacher=np.array(Y,dtype='f4'),base=np.array(B,dtype='f4'),split=np.array(S),outcome=np.array(O,dtype='f4'))
save('records.json',records)
save('collection.json',dict(games=accepted,positions=len(X),split_counts=[S.count(i) for i in range(3)],timeouts=timeouts,seconds=time.monotonic()-start,teacher_depth=2,teacher_qdepth=4,source_sha256=hashlib.sha256((ROOT/'baseline.c').read_bytes()).hexdigest()))
