import urllib.request,pathlib,json
D=pathlib.Path(__file__).resolve().parent
H={'Authorization':'Bearer '+pathlib.Path.home().joinpath('.config/codingame/token').read_text().strip()}
def get(path):
 return urllib.request.urlopen(urllib.request.Request('https://www.codingame.com/api/v1/'+path,headers=H),timeout=45).read()
b=json.loads((D/'battles.json').read_text())['battles']
(D/'all-battles.json').write_text(json.dumps(b,indent=2))
(D/'progress.json').write_bytes(get('puzzles/cultist-wars/progress'))
selected=[x for x in b if x.get('done') and x['my_position']>0]+[x for x in b if x.get('done') and x['my_position']==0][:10]
for x in selected:
 p=D/f"game-{x['game_id']}.txt"
 if not p.exists(): p.write_bytes(get('games/'+str(x['game_id'])))
print('Downloaded',len(selected),'games')
