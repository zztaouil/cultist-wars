"""Fast arena/boss iteration. Token: $CG_TOKEN_FILE (default ../../.cg_token), never printed.
  python3 iter.py submit TAG SRC[:M=V...]   submit to arena, archive in submissions/
  python3 iter.py arena                     live progress + W/D/L vs top players and boss
  python3 iter.py boss TAG SRC[:M=V...]     replay arena boss games (same map/seat), 4 threads
  python3 iter.py bsum TAG...               summarize boss replays"""
import json,os,sys,time,re,hashlib,threading,urllib.request,concurrent.futures as F
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent.parent
os.environ.setdefault('CG_TOKEN_FILE',str(ROOT/'.cg_token'))
sys.path.insert(0,str(ROOT/'validation/race'));from cgapi import req
BOSS_AGENT=4611894
BOSS_GAMES=[g for g in sorted(int(f.stem.split('-')[-1]) for f in (ROOT/'validation/race/boss').glob('base-*.json')) if 'BOSS 3' in json.loads((ROOT/f'validation/race/boss/base-{g}.json').read_text()).get('log','')[:400]]
BD=HERE/'boss';BD.mkdir(exist_ok=True)
def code(spec):
  src,*fl=spec.split(':');s=(ROOT/src).read_text()
  return ''.join(f'#define {f.split("=")[0]} {f.split("=")[1]}\n' for f in fl)+s
def pub(p,body):
  r=urllib.request.Request('https://www.codingame.com/services/'+p,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
  return json.load(urllib.request.urlopen(r,timeout=40))
def submit(tag,spec):
  c=code(spec);p=req('/puzzles/cultist-wars/submissions',dict(language='C',code=c));sid=p.get('submission_id')
  if not sid: print(json.dumps(p)[:500]);return
  (ROOT/'submissions'/f'{sid}.c').write_text(c)
  (ROOT/'submissions'/f'{sid}.json').write_text(json.dumps(dict(submission_id=sid,tag=tag,spec=spec,sha256=hashlib.sha256(c.encode()).hexdigest(),submitted=time.strftime('%FT%T')),indent=1))
  print('SUBMITTED',sid,tag,spec)
def arena():
  lb=req('/puzzles/cultist-wars/leaderboard?limit=100');rank={r['agent_id']:r['rank'] for r in lb['rows']}
  me=lb['me'];g=req('/puzzles/cultist-wars/progress')
  print('ME',json.dumps({k:me.get(k) for k in('rank','league','score','percentage','agent_id')}),'sub',g.get('submission_id'),'pct',g.get('percentage'),'lrank',g.get('league_rank'),'boss',json.dumps(g.get('league_boss')))
  b=pub('gamesPlayersRanking/findLastBattlesByAgentId',[me['agent_id'],None]);agg={}
  for x in b:
    if not x.get('done'):continue
    m=[p for p in x['players'] if p['playerAgentId']==me['agent_id']][0];o=[p for p in x['players'] if p['playerAgentId']!=me['agent_id']][0]
    r=(m['position']<o['position'])-(m['position']>o['position']);a=o['playerAgentId'];rk=rank.get(a,999)
    for k in (['ALL']+(['BOSS'] if a==BOSS_AGENT else [])+(['top5'] if rk<=5 else [])+(['top10'] if rk<=10 else [])+(['top20'] if rk<=20 else [])):
      agg.setdefault(k,[0,0,0])[1-r]+=1
    agg.setdefault('opp:%s#%s'%(o.get('nickname') or 'BOSS',rk),[0,0,0])[1-r]+=1
  for k,v in sorted(agg.items(),key=lambda kv:(not kv[0].isupper() and not kv[0].startswith('top'),kv[0])):
    n=sum(v);print(f'{k:24s} {v[0]}W/{v[1]}D/{v[2]}L  {100*(v[0]+v[1]/2)/n:.0f}%')
lock=threading.Lock()
def poll(tid):
  tok=open(os.environ['CG_TOKEN_FILE']).read().strip()
  b=urllib.request.urlopen(urllib.request.Request('https://www.codingame.com/api/v1/test-games/%s'%tid,headers={'Authorization':'Bearer '+tok}),timeout=50).read().decode()
  if b.lstrip().startswith('{'):return json.loads(b)
  st=b.split('\n',1)[0].split(':',1)[1].strip();return dict(status=st,log=b,test_game_id=tid)
def one(tag,c,spec,g):
  out=BD/f'{tag}-{g}.json'
  if out.exists() and json.loads(out.read_text()).get('status')=='done':return
  while True:
    try:p=req('/puzzles/cultist-wars/test-games',dict(language='C',code=c,replay_game_id=g))
    except Exception as e:time.sleep(5);continue
    if p.get('test_game_id'):break
    lim=p.get('limits') or {};print('wait',json.dumps(p)[:200],flush=True);time.sleep(30)
  tid=p['test_game_id']
  while p.get('status') not in('done','failed'):
    try:time.sleep(4);p=poll(tid)
    except Exception as e:time.sleep(5);p={}
  p['_spec']=spec;out.write_text(json.dumps(p))
  with lock:print(tag,g,p['status'],parse(p),flush=True)
def parse(p):
  log=p.get('log') or '';me=None;sc=None
  for l in log.splitlines():
    m=re.match(r'\s+(\d) .*\(me',l)
    if m:me=int(m.group(1))
    if 'scores:' in l:sc=dict(x.split('=') for x in l.split('scores:')[1].split())
  if me is None or not sc:return None
  s=[int(sc['0']),int(sc['1'])];return me,s[me],s[1-me]
def boss(tag,spec):
  c=code(spec)
  with F.ThreadPoolExecutor(1) as ex:list(ex.map(lambda g:one(tag,c,spec,g),BOSS_GAMES))
  bsum([tag])
def bsum(tags):
  for tag in tags:
    rows=[]
    for f in sorted(BD.glob(f'{tag}-9*.json')):
      p=json.loads(f.read_text());r=parse(p) if p.get('status')=='done' else None
      if r:rows.append(r)
    W=sum(x>y for _,x,y in rows);D=sum(x==y for _,x,y in rows);n=len(rows)
    s1=[r for r in rows if r[0]==1]
    print(f"{tag:12s} n={n} {W}W/{D}D/{n-W-D}L {100*(W+D/2)/max(1,n):.0f}% margin {sum(x-y for _,x,y in rows)/max(1,n):+.2f} seat1 {sum(x>y for _,x,y in s1)}W/{sum(x<y for _,x,y in s1)}L of {len(s1)}")
if __name__=='__main__':
  a=sys.argv[1:]
  {'submit':lambda:submit(a[1],a[2]),'arena':arena,'boss':lambda:boss(a[1],a[2]),'bsum':lambda:bsum(a[1:])}[a[0]]()
