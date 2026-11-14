import pathlib,json,urllib.request
R=pathlib.Path(__file__).resolve().parent
h={'Authorization':'Bearer '+pathlib.Path.home().joinpath('.config/codingame/token').read_text().strip()}
for b in json.loads((R/'battles-latest.json').read_text())['battles']:
 if not b['done']:continue
 p=R/f"game-{b['game_id']}.txt"
 if p.exists():continue
 req=urllib.request.Request('https://www.codingame.com/api/v1/games/'+str(b['game_id']),headers=h)
 p.write_bytes(urllib.request.urlopen(req,timeout=45).read())
 print(b['game_id'],'saved')
