"""Authorized private BOSS 3 tests. Never submits or edits the IDE."""
import json,time,urllib.request,urllib.error,re,hashlib,argparse
from pathlib import Path
D=Path(__file__).resolve().parent;OUT=D/'boss';OUT.mkdir(exist_ok=True)
API='https://www.codingame.com/api/v1'
def request(path,body=None):
 while True:
  token=(Path.home()/'.config/codingame/token').read_text().strip()
  req=urllib.request.Request(API+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
  try:
   with urllib.request.urlopen(req,timeout=60) as r:p=json.load(r)
  except urllib.error.HTTPError as e:
   p=json.load(e)
   if e.code==429:
    delay=max(int(e.headers.get('Retry-After','0')),p.get('retry_after_seconds',60));print('rate limit; waiting',delay,flush=True);time.sleep(delay+1);continue
   raise RuntimeError(json.dumps(p))
  limits=p.get('limits',{})
  for kind in ('requests','test_games'):
   w=limits.get(kind,[]);w=[w] if isinstance(w,dict) else w
   delay=max([x.get('resets_in_seconds',0) for x in w if x.get('remaining',1)==0]+[0])
   if delay:print('cooldown',kind,delay,flush=True);time.sleep(delay+1)
  return p

def parse(p):
 log=p.get('log','');seat=int(re.search(r'^  (\d+) hexagonee',log,re.M)[1]);scores=list(map(int,re.search(r'scores: 0=(-?\d+) 1=(-?\d+)',log).groups()))
 assert re.search(r'^  '+str(1-seat)+r' BOSS 3 \(agent_id 4611894\)',log,re.M), 'Wrong boss'
 stats=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(-?\d+)',line)) for line in log.splitlines() if 'stderr: depth=' in line]
 return dict(seat=seat,scores=scores,result='W' if scores[seat]>scores[1-seat] else 'L' if scores[seat]<scores[1-seat] else 'D',error=bool(re.search(r'not provided|timeout|invalid action|error:',log,re.I)),max_us=max((x.get('elapsed_us',0) for x in stats),default=0),rng_zero=sum(x.get('rng')==0 for x in stats))

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--extra',action='store_true');ap.add_argument('--variant',default='contest');args=ap.parse_args()
 reserve=json.loads((D/'reserve-maps.json').read_text())
 cases=[('fresh-'+str(x[0]),dict(opponents=[4611894],game_options='seed='+str(x[0]))) for x in reserve[:2]]+[(f'replay-{gid}',dict(replay_game_id=gid)) for gid in (921359954,921338908)]
 variants=['baseline','contest','extension','combined']
 if args.extra:
  cases=[('fresh-'+str(x[0]),dict(opponents=[4611894],game_options='seed='+str(x[0]))) for x in reserve[2:4]]+[(f'replay-{gid}',dict(replay_game_id=gid)) for gid in (921345459,921338102)]
  variants=['baseline',args.variant]
 summary=[]
 for i,(case,options) in enumerate(cases):
  order=variants[i%len(variants):]+variants[:i%len(variants)]
  for v in order:
   dest=OUT/f'{case}-{v}.json';src=(D/(v+'.c')).read_text()
   p=json.loads(dest.read_text()) if dest.exists() else None
   if not p:
    p=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=src,**options));dest.write_text(json.dumps(p,indent=2))
   while p.get('status') not in ('done','failed'):
    p=request('/test-games/'+str(p['test_game_id'])+'?wait=30');dest.write_text(json.dumps(p,indent=2))
   if p['status']=='failed':raise RuntimeError(p)
   r=dict(case=case,variant=v,sha256=hashlib.sha256(src.encode()).hexdigest(),**parse(p));summary.append(r);(OUT/('summary-extra.json' if args.extra else 'summary.json')).write_text(json.dumps(summary,indent=2));print(json.dumps(r),flush=True)
if __name__=='__main__':main()
