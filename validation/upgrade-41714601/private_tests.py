import sys,json,time,re,urllib.request,urllib.error
from pathlib import Path
D=Path(__file__).resolve().parent
TOKEN=Path.home().joinpath('.config/codingame/token')
def request(path,body=None):
 req=urllib.request.Request('https://www.codingame.com/api/v1'+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+TOKEN.read_text().strip(),'Content-Type':'application/json'})
 try:
  with urllib.request.urlopen(req,timeout=45) as r:
   text=r.read().decode()
 except urllib.error.HTTPError as e:
  p=json.loads(e.read());print(p,flush=True)
  if e.code==429:time.sleep(p['retry_after_seconds']);return request(path,body)
  raise
 try:return json.loads(text)
 except ValueError:return dict(status='done',log=text)
variant=sys.argv[1]
code=(D/(variant+'.c')).read_text()
for gid in [921574271,921574605,921572974,921574680]:
 path=D/f'judge-{variant}-{gid}.json'
 if path.exists():continue
 r=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=code,replay_game_id=gid))
 print(variant,gid,{k:v for k,v in r.items() if k in ['status','test_game_id','limits']},flush=True)
 while r.get('status') not in ('done','failed'):
  ident=r['test_game_id'];r=request('/test-games/'+str(ident)+'?wait=30')
 path.write_text(json.dumps(r,indent=2))
 log=r.get('log','');print(variant,gid,re.findall(r'scores:.*|ranking.*|error:.*|timeout.*',log)[-5:],flush=True)
 if r.get('status')=='failed':break
