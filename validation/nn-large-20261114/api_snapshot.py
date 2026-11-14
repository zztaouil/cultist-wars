import json, urllib.request
from pathlib import Path
R=Path(__file__).resolve().parent
for name, path in [('limits','/limits?puzzle=cultist-wars'),('progress','/puzzles/cultist-wars/progress'),('opponents','/puzzles/cultist-wars/opponents')]:
    token=(Path.home()/'.config/codingame/token').read_text().strip()
    req=urllib.request.Request('https://www.codingame.com/api/v1'+path,headers={'Authorization':'Bearer '+token})
    with urllib.request.urlopen(req,timeout=45) as f: data=json.load(f)
    (R/(name+'.json')).write_text(json.dumps(data,indent=2))
    print(name, json.dumps(data) if name!='opponents' else 'saved',flush=True)
