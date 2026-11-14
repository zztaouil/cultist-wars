import json,hashlib,subprocess,collections
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parent/'nn-41710886/arena'
games={}
for l in (R/'recon78.txt').read_text().splitlines():
 if l.startswith('G '):_,gid,w=l.split();g={'walls':list(map(int,w)),'states':[],'actions':[]};games[gid]=g
 elif l.startswith('S '):s,a=json.loads('['+l[2:]+']');g['states'].append(s);g['actions'].append(a)
rows={x['id']:x for x in json.loads((R/'study78.json').read_text()) if x['seat']==1 and x['result']=='L' and x.get('verified')}
fixtures=[]
for gid in rows:
 g=games[gid];rec=[0,0];selected={}
 for t,(s,n) in enumerate(zip(g['states'],g['states'][1:])):
  old=rec[0]-rec[1]
  for i in range(14):
   if s[4*i+1] and s[4*i+2]==2 and n[4*i+2]<2:rec[n[4*i+2]]+=1
  if rec[0]-rec[1]>max(0,old):
   # Exclude initial one-recruit initiative: deficit >=2; capture earliest widening.
   if rec[0]-rec[1]>=2 and 'deficit_widens' not in selected:selected['deficit_widens']=(t,rec[:])
  if t%2==0:
   for i in range(14):
    if s[4*i+1] and s[4*i+2]==2 and n[4*i+2]==0:
     prior=t-1
     if prior<1:continue
     pre=g['states'][prior];leader=next((j for j in range(14) if pre[j*4+1] and pre[j*4+2]==1 and pre[j*4+3]),None)
     if leader is not None and pre[4*i+2]==2:
      a,b=pre[leader*4],pre[i*4]
      if abs(a%13-b%13)+abs(a//13-b//13)==1 and 'reachable_lost' not in selected:selected['reachable_lost']=(t,rec[:])
 for kind,(t,count) in selected.items():
  own=t if t%2 else t-1
  for turn in sorted({max(1,own-2),own}):fixtures.append(dict(game=gid,turn=turn,kind=kind,event_turn=t,recruits=count,walls=g['walls'],states=g['states'][:turn+1],actions=g['actions'][:turn+1]))
# Add neighboring turns around the delayed exchange, and earlier combat controls.
g=games['921365647']
for t in (15,17,19,21,23):fixtures.append(dict(game='921365647',turn=t,kind='horizon_neighbor',walls=g['walls'],states=g['states'][:t+1],actions=g['actions'][:t+1]))
(D/'fixtures.json').write_text(json.dumps(fixtures))
def key(w):
 r=[w[i:i+13] for i in range(0,91,13)];return hashlib.sha256(bytes(min(sum(x,[]) for x in (r,r[::-1],[a[::-1] for a in r],[a[::-1] for a in r[::-1]])))).hexdigest()
used={key(g['walls']) for g in games.values()}
# Exclude every map used to train/evaluate the NN, plus all known match maps.
meta=json.loads((D.parent/'nn-41710886/cache/meta.json').read_text());used.update(x['map'] for x in meta['records'])
for f in D.parent.rglob('*maps*.json'):
 if f.parent==D:continue
 try:
  obj=json.loads(f.read_text())
  if isinstance(obj,list):
   for x in obj:
    if isinstance(x,list) and len(x)==3 and isinstance(x[1],list) and len(x[1])==91:used.add(key(x[1]))
 except (ValueError,TypeError):pass
inp=''.join(f'G {seed} {seed} 0\n' for seed in range(41712000,41713000))
p=subprocess.run(['java','-cp',str(D.parent/'nn-41710886/build/java'),'model.Batch'],input=inp,text=True,capture_output=True,check=True)
maps=[]
for l in p.stdout.splitlines():
 if l.startswith('G '):_,gid,w=l.split();w=list(map(int,w))
 elif l.startswith('S '):
  s,_=json.loads('['+l[2:]+']');k=key(w)
  if k not in used:maps.append([int(gid),w,s]);used.add(k)
  if len(maps)==32:break
assert len(maps)==32
(D/'development-maps.json').write_text(json.dumps(maps[:8]));(D/'holdout-maps.json').write_text(json.dumps(maps[8:24]));(D/'reserve-maps.json').write_text(json.dumps(maps[24:]))
print(json.dumps(dict(fixtures=len(fixtures),kinds=dict(collections.Counter(x['kind'] for x in fixtures)),development=8,holdout=16,reserve=8)))
