"""Remaining six private boss tests; stop on any failed request or limit."""
from cg import D,request,save
for seed in range(2001,2004):
 for variant in (['lmr','baseline'] if seed%2 else ['baseline','lmr']):
  name=f'boss-{variant}-{seed}.json'
  assert not (D/name).exists(), 'Refuse to overwrite an existing test.'
  code=(D/('baseline.c' if variant=='baseline' else 'cg-lmr.c')).read_text()
  p=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=code,opponents=['boss'],game_options=f'seed={seed}'))
  save(name,p)
  while p.get('status')=='running':
   p=request('/test-games/'+str(p['test_game_id'])+'?wait=30');save(name,p)
  if p.get('status')!='done':raise SystemExit('Test failed; inspect saved response.')
  if any(v.get('remaining',1)==0 for v in p.get('limits',{}).values()):raise SystemExit('Limit reached; resume only after reset.')
