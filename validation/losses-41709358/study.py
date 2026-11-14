"""Offline, submission-filtered replay reconstruction. Never starts CG games."""
import json,re,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent;ROOT=D.parents[1]
games=json.loads((D/'games.json').read_text());out=D/'java';out.mkdir(exist_ok=True)
subprocess.run(['javac','-d',str(out),*map(str,(ROOT/'source/src/main/java/model').glob('*.java')),str(ROOT/'validation/arena-study/scripts/Batch.java')],check=True)
lines=[]
for g in games:
 acts=g['actions'];acts=acts[:acts.index('TIMEOUT')] if 'TIMEOUT' in acts else acts
 lines += [f"G {g['id']} {re.search(r'seed=(-?\d+)',g['ref'])[1]} {len(acts)}",*acts]
p=subprocess.run(['java','-cp',str(out),'model.Batch'],input='\n'.join(lines)+'\n',capture_output=True,text=True,check=True)
(D/'reconstruction.txt').write_text(p.stdout)
recon={}
for line in p.stdout.splitlines():
 if line.startswith('G '):
  _,gid,w=line.split();cur={'walls':[int(c) for c in w],'states':[],'actions':[]};recon[int(gid)]=cur
 elif line.startswith('S '):
  s,a=json.loads('['+line[2:]+']');cur['states'].append(s);cur['actions'].append(a)
 elif line.startswith('X '):raise RuntimeError(line)
def counts(s):return [sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1)]
rows=[]
for g in games:
 r=recon[g['id']];ss=r['states'];me=next(a['index'] for a in g['agents'] if a['agentId']==6880547);opp=next(a for a in g['agents'] if a['index']!=me)
 assert min(g['scores'])<0 or counts(ss[-1])==g['scores'],(g['id'],counts(ss[-1]),g['scores'])
 events=[];shots=[]
 for t,(s,z) in enumerate(zip(ss,ss[1:])):
  for i in range(14):
   hp,hp2=s[4*i+1],z[4*i+1];own,own2=s[4*i+2],z[4*i+2]
   if hp2<hp:shots.append(dict(turn=t,unit=i,owner=own,damage=hp-hp2,leader=bool(s[4*i+3]),action=g['actions'][t]))
   if own!=own2 and hp2>0:events.append(dict(turn=t,kind='neutral_convert' if own==2 else 'enemy_convert',unit=i,owner=own2,action=g['actions'][t]))
   if hp>0 and hp2==0:events.append(dict(turn=t,kind='death',unit=i,owner=own,action=g['actions'][t]))
 cs=[counts(s) for s in ss];diff=[c[me]-c[1-me] for c in cs]
 row=dict(id=g['id'],seat=me,opponent=opp['pseudo'],opponent_agent=opp['agentId'],scores=g['scores'],timeout=min(g['scores'])<0,outcome='W' if g['scores'][me]>g['scores'][1-me] else 'L' if g['scores'][me]<g['scores'][1-me] else 'D',turns=len(ss)-1,counts={str(t):cs[min(t,len(cs)-1)] for t in [0,20,40,60,100,150]},permanent_deficit=next((t for t in range(len(diff)) if max(diff[t:])<0),None),events=events,shots=shots)
 rows.append(row)
(D/'rows.json').write_text(json.dumps(rows,indent=2));(D/'positions.json').write_text(json.dumps(recon))
from collections import Counter,defaultdict
print('validated',len(rows),'transitions',sum(r['turns'] for r in rows),'outcomes',Counter(r['outcome'] for r in rows))
for seat in (0,1):print('seat',seat,Counter(r['outcome'] for r in rows if r['seat']==seat))
for name in sorted({r['opponent'] for r in rows}):
 group=[r for r in rows if r['opponent']==name];print(name,dict(Counter(r['outcome'] for r in group)))
for r in rows:
 if r['outcome']=='L' and r['opponent'] in ['BOSS','Lv0Sys','YALOKGAR']:
  print('\nLOSS',r['id'],r['opponent'],'seat',r['seat'],'counts',r['counts'],'permanent',r['permanent_deficit']);print('events',r['events'])
