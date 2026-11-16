"""Gate G step 3 (reduced): BOSS 3 test games. Arms: base=41715067, cand=r1 Texel; both BUDGET_MS 45.
First seat: fresh seeds vs boss. Second seat: replay_game_id of arena games with BOSS 3 first. Resumable."""
import json,sys,time,re,hashlib
from pathlib import Path
import cg
D=Path(__file__).parent/'boss';D.mkdir(exist_ok=True);R=Path(__file__).resolve().parents[2]
SRC={'base':R/'submissions/41715067.cpp','cand':Path(__file__).parent/'cand_t1v2r.cpp'}
code={k:'#define BUDGET_MS 45\n'+v.read_text() for k,v in SRC.items()}
for k,c in code.items():assert len(c)<100000,(k,len(c))
SEC=[921605168,921604294,921603771,921603697,921598429,921597759,921597656,921587959,921574383,921574271,921574110,921573370,921437962,921436446,921379893,921377857,921377540,921374717,921368268,921363339]
cases=[('s',41714001+i) for i in range(20)]+[('r',g) for g in SEC]
n=int(sys.argv[1]) if len(sys.argv)>1 else 40
for ci,(kind,x) in enumerate(cases[:n]):
  order=['base','cand'] if ci%2==0 else ['cand','base']
  for arm in order:
    out=D/f'{arm}-{kind}{x}.json'
    if out.exists() and json.loads(out.read_text()).get('status')=='done':continue
    body=dict(language='C++',code=code[arm])
    if kind=='s':body.update(opponents=['boss'],game_options='seed='+str(x))
    else:body['replay_game_id']=x
    p=cg.req('/puzzles/cultist-wars/test-games?wait=30',body)
    while p.get('status') not in ('done','failed'):
      if not p.get('test_game_id'):print('ERR',json.dumps(p)[:300],flush=True);time.sleep(60);break
      p=cg.req('/test-games/%s?wait=30'%p['test_game_id'])
    if p.get('status')=='done':out.write_text(json.dumps(p))
    log=p.get('log') or '';sc=re.findall(r'scores: 0=(-?[\d.]+) 1=(-?[\d.]+)',log)
    print(arm,kind,x,p.get('status'),sc[-1] if sc else None,flush=True)
    lim=p.get('limits') or {}
    for v in (lim.values() if isinstance(lim,dict) else []):
      if isinstance(v,dict) and v.get('remaining',9)<1:time.sleep(v.get('resets_in_seconds',30)+2)
    time.sleep(7)
