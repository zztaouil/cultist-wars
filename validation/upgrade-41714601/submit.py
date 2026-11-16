import json,hashlib,urllib.request
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parents[1]
code=(R/'bot.c').read_bytes();m=json.loads((D/'manifest.json').read_text())
assert len(code)<=100000 and hashlib.sha256(code).hexdigest()==m['candidate_sha256']
req=urllib.request.Request('https://www.codingame.com/api/v1/puzzles/cultist-wars/submissions',data=json.dumps({'language':'C','code':code.decode()}).encode(),headers={'Authorization':'Bearer '+Path.home().joinpath('.config/codingame/token').read_text().strip(),'Content-Type':'application/json'})
with urllib.request.urlopen(req,timeout=45) as r: result=json.loads(r.read())
(D/'submission.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
ident=result.get('submission_id')
if ident:
 (R/'submissions'/f'{ident}.c').write_bytes(code)
 (R/'submissions'/f'{ident}.json').write_text(json.dumps(result,indent=2))
