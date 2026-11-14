import json,urllib.request,os,sys,time,concurrent.futures as F
API='https://www.codingame.com/services/'
def post(p,body):
  r=urllib.request.Request(API+p,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
  for i in range(3):
    try:
      with urllib.request.urlopen(r,timeout=40) as f:return json.load(f)
    except Exception as e: err=e;time.sleep(2)
  raise err
H=json.load(open('data/handles.json'))
players=sys.argv[1:]
os.makedirs('data/battles',exist_ok=True);os.makedirs('data/games',exist_ok=True)
ids=set()
import glob
for fn in glob.glob('data/battles/*.json'):
  ids.update(x['gameId'] for x in json.load(open(fn)) if x.get('done'))
for p in players:
  break
  b=post('gamesPlayersRanking/findLastBattlesByTestSessionHandle',[H[p][0],None])
  json.dump(b,open(f'data/battles/{p}.json','w'))
  done=[x['gameId'] for x in b if x.get('done')]
  print(p,len(b),len(done));ids.update(done)
todo=[i for i in ids if not os.path.exists(f'data/games/{i}.json')]
def get(i):
  g=post('gameResult/findByGameId',[str(i),None])
  keep=dict(gameId=i,refereeInput=g['refereeInput'],scores=g['scores'],ranks=g['ranks'],agents=[dict(pseudo=a.get('codingamer',{}).get('pseudo','BOSS'),raw={k:v for k,v in a.items() if k!='codingamer'},agentId=a.get('agentId'),index=a.get('index')) for a in g['agents']],
    frames=[dict(agent=f.get('agentId'),stdout=f.get('stdout',''),summary=f.get('summary',''),gi=f.get('gameInformation','')) for f in g['frames']])
  json.dump(keep,open(f'data/games/{i}.json','w'));return i
with F.ThreadPoolExecutor(6) as ex:
  for _ in ex.map(get,todo):pass
print('games',len(os.listdir('data/games')))
