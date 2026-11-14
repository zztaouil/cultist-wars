"""Collect every saved game involving BOSS 3 (agent 4611894). Read-only on historical evidence."""
import json,gzip,glob,re,os,hashlib
from pathlib import Path
V=Path(__file__).resolve().parent.parent
BOSS=4611894
games=[];skipped=[]
def parse_log(log):
    m=re.search(r'options: seed=(-?\d+)',log);seed=m.group(1) if m else None
    agents={}
    for m in re.finditer(r'^\s+(\d) (.+?) \((?:me, )?agent_id (-?\d+)\)',log,re.M):agents[int(m.group(1))]=(m.group(2),int(m.group(3)),'(me,' in m.group(0))
    acts=[];err=[];cur=None
    for part in re.split(r'^frame ',log,flags=re.M)[1:]:
        hm=re.match(r'(\d+) agent (\d)',part)
        if not hm:continue
        so=re.search(r'^\s+stdout: (.*)$',part,re.M);se=re.search(r'^\s+stderr: (.*)$',part,re.M)
        acts.append(so.group(1).strip() if so else None);err.append(se.group(1).strip() if se else None)
    sc=re.search(r'scores: 0=(-?[\d.]+) 1=(-?[\d.]+)',log)
    return seed,agents,acts,err,(sc.groups() if sc else None)
# test-game JSONs
pats={'race/boss/*.json':None,'race2/boss/*.json':None,'horizon-41711135/boss/*.json':None,'boss-41709358-20261114/game-*.json':None,'nn-41710886/boss5/41*.json':None,'nn-41710886/boss5-second/*.json':None,'strength/boss-*.json':None,'lmr/boss-*.json':None,'nn-large-20261114/**/*.json':None,'tuning/**/*.json':None,'consistency/**/*.json':None,'support-41710886/**/*.json':None}
for pat in pats:
    for f in sorted(glob.glob(str(V/pat),recursive=True)):
        rel=os.path.relpath(f,V)
        if '_invalid' in rel:skipped.append((rel,'_invalid dir'));continue
        try:o=json.load(open(f))
        except Exception as e:continue
        if not isinstance(o,dict) or 'log' not in o or not isinstance(o['log'],str):continue
        seed,agents,acts,err,sc=parse_log(o['log'])
        games.append(dict(src=rel,kind='test',id=str(o.get('test_game_id')),seed=seed,agents=agents,actions=acts,stderr=err,status=o.get('status'),scores=list(sc) if sc else None,spec=o.get('_spec'),sha=o.get('_sha'),tail=o['log'][-300:]))
for f in sorted(glob.glob(str(V/'nn-41710886/arena/game-*.txt')))+sorted(glob.glob(str(V/'nn-41710886/boss5*/9*.txt'))):
    log=open(f).read();seed,agents,acts,err,sc=parse_log(log)
    games.append(dict(src=os.path.relpath(f,V),kind='arena-log',id=re.search(r'game (\d+)',log).group(1),seed=seed,agents=agents,actions=acts,stderr=err,scores=list(sc) if sc else None,tail=log[-300:]))
for f in ['arena-study/results/games-2026-11-13.jsonl.gz','race/arena-41710268/games.jsonl.gz']:
    for l in gzip.open(V/f,'rt'):
        o=json.loads(l);ag={a['index']:(a['pseudo'],a.get('agentId'),a['pseudo']=='hexagonee') for a in o['agents']}
        games.append(dict(src=f,kind='arena',id=str(o['id']),seed=o['ref'].strip().split('=')[1],agents=ag,actions=o['actions'],stderr=None,scores=o['scores'],keys=list(o.keys())))
json.dump(dict(games=games,skipped=skipped),open(Path(__file__).parent/'all_games.json','w'))
from collections import Counter
c=Counter()
for g in games:
    ids=[a[1] for a in g['agents'].values()]
    c[(g['src'].rsplit('/',1)[0],BOSS in ids, any(a[2] for a in g['agents'].values()))]+=1
for k,v in sorted(c.items()):print(k,v)
print('skipped',len(skipped))
