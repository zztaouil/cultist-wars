import json,hashlib
from pathlib import Path
from judge import request,D
source=Path('bot.cpp').read_text()
assert len(source.encode())<100000
receipt=D/'submission.json'
if receipt.exists():raise SystemExit('Submission receipt already exists; refusing duplicate.')
r=request('/puzzles/cultist-wars/submissions',{'language':'C++','code':source})
receipt.write_text(json.dumps(r,indent=2))
print(json.dumps(r),flush=True)
sid=r.get('submission_id')
if sid:
 Path(f'submissions/{sid}.cpp').write_text(source)
 Path(f'submissions/{sid}.json').write_text(json.dumps(r,indent=2))
 (D/'submitted.cpp').write_text(source)
 (D/'submitted-sha256.txt').write_text(hashlib.sha256(source.encode()).hexdigest()+'\n')
 p=request('/puzzles/cultist-wars/progress');(D/'submission-progress.json').write_text(json.dumps(p,indent=2));print(json.dumps(p))
