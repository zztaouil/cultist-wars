"""E0: fetch all battles + game logs of a submission, count own timeouts. Resumable. Token never printed."""
import urllib.request,pathlib,json,time,sys,re
D=pathlib.Path(__file__).resolve().parent;R=D.parents[2]
tok=(R/'.cg_token').read_text().strip();H={'Authorization':'Bearer '+tok}
def get(p):
  for i in range(3):
    try:return urllib.request.urlopen(urllib.request.Request('https://www.codingame.com/api/v1/'+p,headers=H),timeout=40).read()
    except urllib.error.HTTPError as e:
      if e.code==429:time.sleep(20);continue
      raise
sid=sys.argv[1];T=time.time()+float(sys.argv[2]) if len(sys.argv)>2 else 1e18
bf=D/f'battles-{sid}.json'
if not bf.exists():
  bs=[];before=None
  while True:
    d=json.loads(get(f'puzzles/cultist-wars/battles?submission={sid}&limit=100'+(f'&before={before}' if before else '')));bs+=d.get('battles',[]);before=d.get('next_before')
    if not before:break
  bf.write_text(json.dumps(bs))
bs=json.loads(bf.read_text());n=0
for b in bs:
  if not b.get('done'):continue
  p=D/'games'/f"{b['game_id']}.txt"
  if p.exists():continue
  if time.time()>T:break
  p.write_bytes(get('games/'+str(b['game_id'])));n+=1;time.sleep(0.45)
have=[b for b in bs if b.get('done') and (D/'games'/f"{b['game_id']}.txt").exists()]
print('battles',len(bs),'done',sum(1 for b in bs if b.get('done')),'fetched',len(have),'new',n)
