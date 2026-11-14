"""Authorized private boss tests only. Unique output names; never submit."""
import sys,json,time,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from cg import request
D=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--candidate',default='eval-hp.c');ap.add_argument('--prefix',default='hp');ap.add_argument('--seeds',type=int,nargs='+',default=[2000,2001,2002,2003]);a=ap.parse_args()
for seed in a.seeds:
 for variant in (['baseline',a.prefix] if seed%2==0 else [a.prefix,'baseline']):
  path=D/f'boss-{variant}-{seed}.json'
  if path.exists():continue
  src=D/('baseline.c' if variant=='baseline' else a.candidate)
  p=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=src.read_text(),opponents=['boss'],game_options='seed='+str(seed)))
  path.write_text(json.dumps(p,indent=2))
  while p.get('status') not in ('done','failed'):
   if not p.get('test_game_id'):raise RuntimeError(p)
   p=request('/test-games/'+str(p['test_game_id'])+'?wait=30');path.write_text(json.dumps(p,indent=2))
  print(json.dumps(dict(file=path.name,status=p['status'],limits=p.get('limits'))),flush=True)
  if p['status']=='failed':raise RuntimeError('Private test failed; inspect saved response')
  lim=p.get('limits',{});windows=lim.get('test_games',[])
  if isinstance(windows,dict):windows=[windows]
  wait=max([w.get('resets_in_seconds',0) for w in windows if w.get('remaining',1)<2]+[0])
  if wait:time.sleep(wait+1)
