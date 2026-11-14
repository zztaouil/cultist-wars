"""Replay arena boss games (same seat, same map) with a candidate source.
  python3 bosstest.py TAG SRC[:MACRO=V...] --budget 160 [--games id ...]
Resumable: one JSON per (tag, game) under boss/. Summarize: --summary TAG..."""
import json,sys,time,argparse,hashlib,re
sys.path.insert(0,'.')
from cgapi import req
from pathlib import Path
B=Path('boss');B.mkdir(exist_ok=True)
GAMES=json.load(open("gate_games.json"))
def code(spec):
  src,*fl=spec.split(':');s=open(src).read()
  return ''.join(f'#define {f.split("=")[0]} {f.split("=")[1]}\n' for f in fl)+s
def result(p):
  log=p.get('log') or ''
  m=re.search(r'scores?:?\s*(.*)',log.splitlines()[-1] if log else '')
  return log
ap=argparse.ArgumentParser();ap.add_argument('tag',nargs='+');ap.add_argument('--src');ap.add_argument('--budget',type=int,default=160)
ap.add_argument('--games',type=int,nargs='*',default=GAMES);ap.add_argument('--summary',action='store_true')
a=ap.parse_args()
if not a.summary:
  tag=a.tag[0];c=code(a.src);t0=time.time()
  for g in a.games:
    out=B/f'{tag}-{g}.json'
    if out.exists() and json.loads(out.read_text()).get('status')=='done': continue
    if time.time()-t0>a.budget: print('budget; rerun');break
    p=req('/puzzles/cultist-wars/test-games?wait=30',dict(language='C',code=c,replay_game_id=g))
    while p.get('status') not in ('done','failed'):
      if not p.get('test_game_id'): print(json.dumps(p)[:400]);sys.exit(1)
      p=req('/test-games/%s?wait=30'%p['test_game_id'])
    p['_spec']=a.src;p['_sha']=hashlib.sha256(c.encode()).hexdigest()[:12]
    out.write_text(json.dumps(p));print(tag,g,p['status'],{k:p.get(k) for k in ('scores','ranks','my_index','agents') if k in p},flush=True)
    lim=(p.get('limits') or {}).get('test_games')
    if isinstance(lim,dict) and lim.get('remaining',9)<2: time.sleep(lim.get('resets_in_seconds',0)+1)
def parse(p):
  log=p.get('log') or '';me=None
  for l in log.splitlines():
    m=re.match(r'\s+(\d) .*\(me',l)
    if m: me=int(m.group(1))
    if 'scores:' in l: sc=dict(x.split('=') for x in l.split('scores:')[1].split())
  s=[int(sc['0']),int(sc['1'])];return me,s[me],s[1-me]
if a.summary:
  for tag in a.tag:
    rows=[]
    for f in sorted(B.glob(f'{tag}-*.json')):
      p=json.loads(f.read_text())
      if p.get('status')!='done': continue
      me,x,y=parse(p);rows.append((int(f.stem.split('-')[-1]),me,x,y))
    W=sum(x>y for _,_,x,y in rows);D=sum(x==y for _,_,x,y in rows);L=len(rows)-W-D
    s1=[r for r in rows if r[1]==1]
    print(f"{tag:10s} n={len(rows):2d} {W}W/{D}D/{L}L score {100*(W+D/2)/max(1,len(rows)):.1f}%  margin {sum(x-y for _,_,x,y in rows)/max(1,len(rows)):+.2f}  seat1 {sum(x>y for _,_,x,y in s1)}W/{sum(x==y for _,_,x,y in s1)}D/{sum(x<y for _,_,x,y in s1)}L  | "+' '.join(f"{g%10000}:{x}-{y}" for g,_,x,y in rows))
