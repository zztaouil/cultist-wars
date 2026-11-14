"""Eight predeclared matched cases; 16 private tests; no submission/IDE writes."""
import hashlib, importlib.util, json, time, urllib.request, urllib.error
from pathlib import Path
R=Path(__file__).resolve().parent; OUT=R/'boss';OUT.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('oldboss',R.parent/'horizon-41711135/boss.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
def request(path,body=None):
    token=(Path.home()/'.config/codingame/token').read_text().strip()
    req=urllib.request.Request('https://www.codingame.com/api/v1'+path,data=None if body is None else json.dumps(body).encode(),headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=45) as f: return json.load(f)
    except urllib.error.HTTPError as e:
        detail=e.read().decode();raise RuntimeError(f'HTTP {e.code}: {detail}') from None
cases=[]
for i,gid in enumerate((921359954,921359519,921345459,921338908)):
    cases.append((f'seat1-{gid}',dict(replay_game_id=gid),1))
    seed=41713001+i;cases.append((f'seat0-{seed}',dict(opponents=[4611894],game_options=f'seed={seed}'),0))
(R/'boss-cases.json').write_text(json.dumps(cases,indent=2));summary=[]
for i,(case,options,seat) in enumerate(cases):
    for variant in (('baseline','candidate') if i%2==0 else ('candidate','baseline')):
        dest=OUT/f'{case}-{variant}.json';src=(R/f'{variant}.c').read_text()
        p=json.loads(dest.read_text()) if dest.exists() else None
        if p is None:
            quota=request('/limits?puzzle=cultist-wars')
            exhausted=[w for w in quota['test_games'] if w['remaining']==0]
            if exhausted: raise RuntimeError('Test quota exhausted: '+json.dumps(exhausted))
            p=request('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=src,**options));dest.write_text(json.dumps(p,indent=2))
        while p.get('status') not in ('done','failed'):
            p=request('/test-games/'+str(p['test_game_id'])+'?wait=30');dest.write_text(json.dumps(p,indent=2))
        if p['status']=='failed':raise RuntimeError(str(p))
        result=mod.parse(p);assert result['seat']==seat
        row=dict(case=case,variant=variant,sha256=hashlib.sha256(src.encode()).hexdigest(),**result)
        summary.append(row);(OUT/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(row),flush=True)
