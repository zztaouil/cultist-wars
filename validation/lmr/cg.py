"""Read CG state or run private boss tests. No submission/IDE mutation endpoints."""
import argparse,json,urllib.request,urllib.error
from pathlib import Path
D=Path(__file__).resolve().parent
API='https://www.codingame.com/api/v1'
def request(path,body=None):
 token=Path.home().joinpath('.config/codingame/token').read_text().strip()
 headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'}
 req=urllib.request.Request(API+path,data=json.dumps(body).encode() if body is not None else None,headers=headers)
 try:
  with urllib.request.urlopen(req,timeout=45) as r:return json.load(r)
 except urllib.error.HTTPError as e:
  p=json.load(e);print(json.dumps(p));raise SystemExit(1)
def save(name,p):
 (D/name).write_text(json.dumps(p,indent=2))
 print(json.dumps({k:v for k,v in p.items() if k in ['status','test_game_id','poll_url','limits','submission_id','league','league_boss','percentage','rank','eligible_for_promotion']}),flush=True)
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('task',choices=['info','start','poll']);ap.add_argument('--variant',choices=['baseline','lmr']);ap.add_argument('--seed',type=int);ap.add_argument('--id',type=int);ap.add_argument('--output');a=ap.parse_args()
 if a.task=='info':
  for label,path in [('progress','/puzzles/cultist-wars/progress'),('rules','/puzzles/cultist-wars'),('limits','/limits?puzzle=cultist-wars')]:save('cg-'+label+'.json',request(path))
 elif a.task=='start':
  assert a.variant and a.seed is not None and a.output
  code=(D/('baseline.c' if a.variant=='baseline' else 'cg-lmr.c')).read_text()
  p=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=code,opponents=['boss'],game_options='seed='+str(a.seed)))
  save(a.output,p)
 else:
  assert a.id and a.output
  save(a.output,request('/test-games/'+str(a.id)+'?wait=30'))
