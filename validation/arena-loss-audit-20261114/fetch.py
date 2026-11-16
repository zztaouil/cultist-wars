import urllib.request,pathlib,json,time
D=pathlib.Path(__file__).resolve().parent
H={'Authorization':'Bearer '+pathlib.Path.home().joinpath('.config/codingame/token').read_text().strip()}
def get(path):
 req=urllib.request.Request('https://www.codingame.com/api/v1/'+path,headers=H)
 return urllib.request.urlopen(req,timeout=45).read()
d=json.loads((D/'battles.json').read_text());bs=d['battles'];page=0
while d.get('next_before') and len(bs)<100:
 page+=1;raw=get('puzzles/cultist-wars/battles?submission=41714430&before='+str(d['next_before']));(D/f'page-{page}.json').write_bytes(raw);d=json.loads(raw);bs+=d['battles']
(D/'all-battles.json').write_text(json.dumps(bs,indent=2))
losses=[b for b in bs if b.get('done') and b['my_position']>0]
controls=[b for b in bs if b.get('done') and b['my_position']==0][:10]
for b in losses+controls:
 p=D/f"game-{b['game_id']}.txt"
 if not p.exists():p.write_bytes(get('games/'+str(b['game_id'])))
print('battles',len(bs),'losses',len(losses),'controls',len(controls))
