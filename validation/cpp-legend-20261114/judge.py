import sys,json,re,urllib.request
from pathlib import Path
D=Path(__file__).resolve().parent
TOKEN=Path.home()/'.config/codingame/token'
def request(path,body=None):
 req=urllib.request.Request('https://www.codingame.com/api/v1'+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+TOKEN.read_text().strip(),'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=45) as r:text=r.read().decode()
 try:return json.loads(text)
 except ValueError:return dict(status='done',log=text)
if __name__=='__main__':
 variant=sys.argv[1];src=Path('bot.cpp') if variant.startswith('candidate') else D/(variant+'.cpp')
 for gid in map(int,sys.argv[2:]):
  path=D/f'judge-{variant}-{gid}.json'
  if path.exists():continue
  r=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C++',code=src.read_text(),replay_game_id=gid))
  while r.get('status') not in ('done','failed'):
   print({k:v for k,v in r.items() if k!='log'},flush=True)
   r=request('/test-games/'+str(r['test_game_id'])+'?wait=30')
  path.write_text(json.dumps(r,indent=2))
  print(variant,gid,re.findall(r'scores:.*|ranking.*|error:.*|timeout.*',r.get('log',''))[-5:],r.get('limits'),flush=True)
