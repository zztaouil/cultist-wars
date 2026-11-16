"""Collect public replay history; only approved teacher players enter the corpus."""
import gzip,json,time,urllib.request,urllib.error
from pathlib import Path

ROOT=Path(__file__).resolve().parent
OLD=ROOT.parent/'imitation-1m-20261114'
TOP={r['nickname'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
for d in ('battles','games'): (ROOT/d).mkdir(exist_ok=True)
last=0
def request(url,body=None,token=False):
 global last
 for attempt in range(6):
  time.sleep(max(0,1.05-(time.monotonic()-last)));last=time.monotonic()
  headers={'Content-Type':'application/json'}
  if token:headers['Authorization']='Bearer '+(Path.home()/'.config/codingame/token').read_text().strip()
  req=urllib.request.Request(url,data=json.dumps(body).encode() if body is not None else None,headers=headers)
  try:
   with urllib.request.urlopen(req,timeout=60) as f:return json.load(f)
  except urllib.error.HTTPError as e:
   if e.code==429:
    delay=max(60,int(e.headers.get('Retry-After','60')));print(json.dumps(dict(rate_limited=True,wait=delay)),flush=True);time.sleep(delay);continue
   if e.code in (400,404,422):
    return {'error':e.code,'details':e.read().decode('utf-8',errors='replace')}
   raise
  except (OSError,ValueError):
   if attempt==5:raise
   time.sleep(5*(attempt+1))
 raise RuntimeError('retry limit')
def post(endpoint,body):return request('https://www.codingame.com/services/'+endpoint,body)
def save(path,obj):
 tmp=path.with_suffix(path.suffix+'.tmp');tmp.write_text(json.dumps(obj));tmp.replace(path)

def saved_games():
 games={}
 for path in [OLD/'games.jsonl.gz',ROOT.parent/'nn-41710886/games.jsonl.gz',ROOT.parent/'arena-study/results/games-2026-11-13.jsonl.gz',ROOT.parent/'race/boss/base_replays.jsonl.gz',ROOT.parent/'race/arena-41710268/games.jsonl.gz']:
  if not path.exists():continue
  for g in map(json.loads,gzip.open(path,'rt')):
   if any(a['pseudo'] in TOP for a in g['agents']):games.setdefault(str(g['id']),g)
 return games

def main():
 roster=ROOT/'arena-roster.json'
 if roster.exists():rows=json.loads(roster.read_text())
 else:
  rows=[]
  for offset in range(0,500,100):
   d=request(f'https://www.codingame.com/api/v1/puzzles/cultist-wars/leaderboard?limit=100&offset={offset}',token=True)
   rows+=d['rows']
   if len(rows)>=d['total']:break
  save(roster,rows)
 old=saved_games()
 ranks={r['agent_id']:r['rank'] for r in rows}
 agents={r['agent_id'] for r in rows};teachers={r['agent_id'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
 # Seed all previously observed teacher versions and source battle IDs.
 ids=set(old)
 for g in old.values():
  for a in g['agents']:
   if a['pseudo'] in TOP:teachers.add(a['agentId'])
 def absorb(b):
  if not isinstance(b,list):return
  for battle in b:
   if not any(p.get('nickname') in TOP for p in battle['players']):continue
   if battle.get('done'):ids.add(str(battle['gameId']))
   for player in battle['players']:
    if player.get('nickname') in TOP:teachers.add(player['playerAgentId'])
 for path in (OLD/'battles').glob('*.json'):absorb(json.loads(path.read_text()))
 seen=set()
 while (agents|teachers)-seen:
  # Teacher histories first; other roster players supply discovery only.
  pending=(teachers-seen) or (agents-seen);agent=min(pending,key=lambda a:(ranks.get(a,10000),a));seen.add(agent)
  path=ROOT/'battles'/f'{agent}.json'
  b=json.loads(path.read_text()) if path.exists() else post('gamesPlayersRanking/findLastBattlesByAgentId',[agent,None])
  save(path,b);absorb(b)
  if len(seen)%10==0:print(json.dumps(dict(phase='discovery',agents=len(seen),pending=len((agents|teachers)-seen),teacher_agents=len(teachers),games=len(ids))),flush=True)
 save(ROOT/'game_ids.json',sorted(ids))
 print(json.dumps(dict(phase='replays',games=len(ids),new_games=len(ids-set(old)))),flush=True)
 for i,gid in enumerate(sorted(ids)):
  path=ROOT/'games'/f'{gid}.json'
  if gid in old or path.exists():continue
  g=post('gameResult/findByGameId',[gid,None])
  if 'frames' not in g:save(path,g);continue
  actions=[]
  for f in g['frames']:
   if 'timeout' in (f.get('summary') or '').lower() or 'timed out' in (f.get('gameInformation') or '').lower():actions.append('TIMEOUT');break
   s=(f.get('stdout') or '').strip()
   if s:actions.append(s.split('\n')[0].split(';')[0].strip())
  result=dict(id=int(gid),ref=g['refereeInput'],scores=g['scores'],agents=[dict(pseudo=a.get('codingamer',{}).get('pseudo','BOSS'),agentId=a.get('agentId'),index=a['index']) for a in g['agents']],actions=actions)
  save(path,result)
  if i%50==0:print(json.dumps(dict(phase='replays',processed=i,total=len(ids))),flush=True)
 allgames=dict(old)
 for path in (ROOT/'games').glob('*.json'):
  g=json.loads(path.read_text())
  if 'actions' in g:allgames[str(g['id'])]=g
 allgames={k:g for k,g in allgames.items() if any(a['pseudo'] in TOP for a in g['agents'])}
 with gzip.open(ROOT/'games.jsonl.gz.tmp','wt') as f:
  for k,g in sorted(allgames.items()):f.write(json.dumps(g)+'\n')
 (ROOT/'games.jsonl.gz.tmp').replace(ROOT/'games.jsonl.gz')
 def turns(g):return sum({a['index']:a['pseudo'] for a in g['agents']}[i%2] in TOP for i in range(len(g['actions'])))
 eligible=sum(turns(g) for g in allgames.values() if 'TIMEOUT' not in g['actions'] and g['actions'])
 report=dict(games=len(allgames),players=sorted(TOP),discovery_agents=len(seen),teacher_agents=len(teachers),raw_expert_turns=sum(map(turns,allgames.values())),eligible_expert_turns_before_reconstruction=eligible,theoretical_max_positions_with_four_reflections=eligible*4)
 save(ROOT/'collection.json',report);print(json.dumps(report),flush=True)
if __name__=='__main__':main()
