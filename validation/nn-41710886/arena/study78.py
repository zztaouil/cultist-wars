import json,re,subprocess,collections,statistics
from pathlib import Path
R=Path(__file__).resolve().parent;bs=json.loads((R/'battles-78.json').read_text())['battles'];rows={};inp=[]
for b in bs:
 if not b['done']:continue
 gid=str(b['game_id']);s=(R/f'game-{gid}.txt').read_text();seat=int(re.search(r'^  (\d+) hexagonee',s,re.M)[1]);score=list(map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',s).groups()));opp=next(x['nickname'] for x in b['players'] if not x.get('me'));stats=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(-?\d+)',l)) for l in s.splitlines() if 'stderr: depth=' in l]
 r={'id':gid,'seat':seat,'opponent':opp,'scores':score,'result':'W' if score[seat]>score[1-seat] else 'D' if score[seat]==score[1-seat] else 'L','error':bool(re.search(r'timeout|not provided|invalid action|error:',s,re.I)),'max_us':max((x['elapsed_us'] for x in stats),default=0),'rng_zero':sum(x.get('rng')==0 for x in stats)};rows[gid]=r
 if r['error']:continue
 acts=re.findall(r'^  stdout: (.*)$',s,re.M);acts=[' '.join(a.split()[:1 if a.startswith('WAIT') else 4 if ' MOVE ' in a else 3]) for a in acts];seed=re.search(r'options: seed=(-?\d+)',s)[1];inp.append(f'G {gid} {seed} {len(acts)}');inp.extend(acts)
r=subprocess.run(['java','-cp',str(R.parent/'build/java'),'model.Batch'],input='\n'.join(inp)+'\n',text=True,capture_output=True,check=True);(R/'recon78.txt').write_text(r.stdout)
for l in r.stdout.splitlines():
 if l.startswith('G '):gid=l.split()[1];states=[];actions=[];bad=False
 elif l.startswith('S '):s,a=json.loads('['+l[2:]+']');states.append(s);actions.append(a)
 elif l.startswith('X '):bad=True
 elif l=='E':
  rec=rows[gid];seat=rec['seat'];count=lambda s:[sum(s[i*4+1]>0 and s[i*4+2]==p for i in range(14)) for p in (0,1)]
  rec['verified']=not bad and count(states[-1])==rec['scores']
  if not rec['verified']:continue
  recruits=[0,0];kills=[0,0];events=[]
  for t,(s,n) in enumerate(zip(states,states[1:])):
   for i in range(14):
    if s[i*4+1]<=0:continue
    if s[i*4+2]==2 and n[i*4+2] in (0,1):recruits[n[i*4+2]]+=1
    if n[i*4+1]==0 and s[i*4+2] in (0,1):kills[1-s[i*4+2]]+=1
   if count(s)!=count(n):events.append([t,count(n)])
  rec.update(recruits=[recruits[seat],recruits[1-seat]],kills=[kills[seat],kills[1-seat]],counts={str(t):[count(states[t])[seat],count(states[t])[1-seat]] for t in (20,40,60,80,100,120,150) if t<len(states)},events=events)
out=list(rows.values());(R/'study78.json').write_text(json.dumps(out,indent=2))
print('OVERALL',dict(collections.Counter(x['result'] for x in out)))
for seat in (0,1):print('SEAT',seat,dict(collections.Counter(x['result'] for x in out if x['seat']==seat)))
opp=collections.defaultdict(collections.Counter)
for x in out:opp[x['opponent']][x['result']]+=1
print('OPPONENTS',dict(opp));print('ERRORS',[x for x in out if x['error']]);print('RNG_ZERO',sum(x['rng_zero'] for x in out));print('VERIFIED',sum(x.get('verified',False) for x in out));print('MAX_MS',max(x['max_us'] for x in out)/1000)
for result in ('W','D','L'):
 a=[x for x in out if x['result']==result and x.get('verified')];print(result,'n',len(a),'recruit deficit',sum(x['recruits'][0]<x['recruits'][1] for x in a),'avg recruit diff',statistics.mean(x['recruits'][0]-x['recruits'][1] for x in a))
print('LOSSES',json.dumps([x for x in out if x['result']=='L']))
