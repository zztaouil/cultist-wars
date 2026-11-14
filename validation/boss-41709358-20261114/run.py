import sys,json,time,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from validation.tuning.cg_submit import request,parse_log
D=Path(__file__).resolve().parent
code=(ROOT/'submissions/41709358.c').read_text()
assert hashlib.sha256(code.encode()).hexdigest()=='82b341ea7cb5ace6f316af95069fd0d763476c0aa5126693b78b8df398368b4d'
for n in range(1,11):
 out=D/f'game-{n:02}.json'
 p=json.loads(out.read_text()) if out.exists() else {}
 if p.get('status')=='done':continue
 if not p.get('test_game_id'):
  while True:
   limits=request('/limits?puzzle=cultist-wars')
   blocked=[w for w in limits['test_games'] if w['remaining']==0]
   if not blocked:break
   delay=max(w['resets_in_seconds'] for w in blocked)+1
   print(json.dumps({'waiting_quota_seconds':delay,'game':n}),flush=True)
   for _ in range((delay+29)//30):time.sleep(min(30,delay));delay-=min(30,delay)
  p=request('/puzzles/cultist-wars/test-games?wait=30',{'language':'C','code':code,'opponents':['4611894'],'game_options':f'seed={141700+n}'})
  out.write_text(json.dumps(p,indent=2))
 while p.get('status') not in ('done','failed'):
  if not p.get('test_game_id'):raise RuntimeError(p)
  p=request('/test-games/%s?wait=30'%p['test_game_id'])
  out.write_text(json.dumps(p,indent=2))
 row={'match':n,'seed':141700+n,'test_game_id':p.get('test_game_id'),'status':p.get('status'),**parse_log(p.get('log'))}
 print(json.dumps(row),flush=True)
 if p.get('status')!='done':raise RuntimeError('Game failed; inspect saved response')
 rows=[]
 for f in sorted(D.glob('game-*.json')):
  g=json.loads(f.read_text())
  rows.append({'match':int(f.stem.split('-')[1]),'test_game_id':g.get('test_game_id'),**parse_log(g.get('log'))})
 (D/'results.json').write_text(json.dumps(rows,indent=2))
