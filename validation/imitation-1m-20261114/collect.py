"""Resume public replay collection for a frozen top-ten player cohort."""
import gzip,json,re,time,urllib.request,urllib.error
from pathlib import Path
ROOT=Path(__file__).resolve().parent
TOP={r['nickname'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
(ROOT/'battles').mkdir(exist_ok=True);(ROOT/'games').mkdir(exist_ok=True)
last=0
def post(endpoint,body):
 global last
 for attempt in range(6):
  time.sleep(max(0,.65-(time.monotonic()-last)));last=time.monotonic()
  req=urllib.request.Request('https://www.codingame.com/services/'+endpoint,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
  try:
   with urllib.request.urlopen(req,timeout=60) as r:return json.load(r)
  except urllib.error.HTTPError as e:
   if e.code==429:time.sleep(max(60,int(e.headers.get('Retry-After','60'))));continue
   if e.code in (404,400):return {'error':e.code}
   if attempt==5:raise
   time.sleep(5*(attempt+1))
  except (OSError,ValueError):
   if attempt==5:raise
   time.sleep(5*(attempt+1))
 raise RuntimeError('retry limit')
old={str(g['id']):g for g in map(json.loads,gzip.open(ROOT.parent/'nn-41710886/games.jsonl.gz','rt'))}
agents={r['agent_id'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
for g in old.values():
 for a in g['agents']:
  if a['pseudo'] in TOP:agents.add(a['agentId'])
ids=set();seen=set()
while agents-seen:
 agent=min(agents-seen);seen.add(agent);path=ROOT/'battles'/f'{agent}.json'
 b=json.loads(path.read_text()) if path.exists() else post('gamesPlayersRanking/findLastBattlesByAgentId',[agent,None])
 path.write_text(json.dumps(b))
 if not isinstance(b,list):continue
 for battle in b:
  if battle.get('done'):ids.add(str(battle['gameId']))
  for p in battle['players']:
   if p['nickname'] in TOP:agents.add(p['playerAgentId'])
 print(json.dumps({'agent':agent,'battles':len(b),'games':len(ids),'agents_remaining':len(agents-seen)}),flush=True)
(ROOT/'game_ids.json').write_text(json.dumps(sorted(ids)))
for i,gid in enumerate(sorted(ids)):
 if gid in old:continue
 path=ROOT/'games'/f'{gid}.json'
 if path.exists():continue
 g=post('gameResult/findByGameId',[gid,None])
 if 'frames' not in g:path.write_text(json.dumps(g));continue
 actions=[]
 for f in g['frames']:
  if 'timeout' in (f.get('summary') or '').lower() or 'timed out' in (f.get('gameInformation') or '').lower():actions.append('TIMEOUT');break
  s=(f.get('stdout') or '').strip()
  if s:actions.append(s.split('\n')[0].split(';')[0].strip())
 result=dict(id=int(gid),ref=g['refereeInput'],scores=g['scores'],agents=[dict(pseudo=a.get('codingamer',{}).get('pseudo','BOSS'),agentId=a.get('agentId'),index=a['index']) for a in g['agents']],actions=actions)
 path.write_text(json.dumps(result))
 if i%50==0:print(json.dumps({'processed':i,'total':len(ids)}),flush=True)
allgames=dict(old)
for path in (ROOT/'games').glob('*.json'):
 g=json.loads(path.read_text())
 if 'actions' in g:allgames[str(g['id'])]=g
allgames={k:g for k,g in allgames.items() if any(a['pseudo'] in TOP for a in g['agents'])}
with gzip.open(ROOT/'games.jsonl.gz','wt') as f:
 for k,g in sorted(allgames.items()):f.write(json.dumps(g)+'\n')
report=dict(games=len(allgames),players=sorted(TOP),agents=len(seen),raw_expert_turns=sum(sum(g['agents'][i%2]['pseudo'] in TOP for i in range(len(g['actions']))) for g in allgames.values()))
(ROOT/'collection.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
