"""Pull finished ranked games of one agent via public CodinGame services (no token)."""
import json,urllib.request,os,sys,time,concurrent.futures as F
API='https://www.codingame.com/services/'
def post(p,body):
  r=urllib.request.Request(API+p,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
  for i in range(3):
    try:
      with urllib.request.urlopen(r,timeout=40) as f:return json.load(f)
    except Exception as e: err=e;time.sleep(2)
  raise err
agent=int(sys.argv[1]);out=sys.argv[2]
b=post('gamesPlayersRanking/findLastBattlesByAgentId',[agent,None])
json.dump(b,open(f'{out}/battles.json','w'))
done=[x['gameId'] for x in b if x.get('done')]
todo=[i for i in done if not os.path.exists(f'{out}/games/{i}.json')]
def get(i):
  g=post('gameResult/findByGameId',[str(i),None])
  keep=dict(gameId=i,refereeInput=g['refereeInput'],scores=g['scores'],ranks=g['ranks'],
    agents=[dict(pseudo=a.get('codingamer',{}).get('pseudo','BOSS'),agentId=a.get('agentId'),index=a.get('index'),score=a.get('score')) for a in g['agents']],
    frames=[dict(agent=f.get('agentId'),stdout=f.get('stdout',''),summary=f.get('summary','')) for f in g['frames']])
  json.dump(keep,open(f'{out}/games/{i}.json','w'));return i
with F.ThreadPoolExecutor(4) as ex: list(ex.map(get,todo))
print('battles',len(b),'done',len(done),'fetched',len(todo))
