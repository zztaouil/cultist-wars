import urllib.request,pathlib,json
D=pathlib.Path(__file__).resolve().parent
H={'Authorization':'Bearer '+pathlib.Path.home().joinpath('.config/codingame/token').read_text().strip()}
b=json.loads((D/'battles.json').read_text())['battles']
selected=[x for x in b if x.get('done') and x['my_position']>0][:8]
for x in selected:
 p=D/f"game-{x['game_id']}.txt"
 if not p.exists():
  with urllib.request.urlopen(urllib.request.Request('https://www.codingame.com/api/v1/games/'+str(x['game_id']),headers=H),timeout=45) as r:p.write_bytes(r.read())
print('Downloaded',len(selected),'recent losses')
