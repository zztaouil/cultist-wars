import json,subprocess
from pathlib import Path
R=Path(__file__).resolve().parent
rows=json.loads((R/'match-maps.json').read_text())[:12];results=[]
for gid,w,s in rows:
 lines=['0 13 7']+[''.join('#' if x else '.' for x in w[y*13:(y+1)*13]) for y in range(7)]
 units=[(i,s[4*i:4*i+4]) for i in range(14) if s[4*i+1]];lines.append(str(len(units)))
 for i,(pos,hp,owner,leader) in units:lines.append(f'{i} {leader} {hp} {pos%13} {pos//13} {owner}')
 p=subprocess.run([str(R/'build/candidate')],input='\n'.join(lines)+'\n',text=True,capture_output=True,check=True,timeout=5)
 assert len(p.stdout.splitlines())==1
 results.append({'map':gid,'action':p.stdout.strip(),'diagnostics':p.stderr.strip()})
(R/'protocol.json').write_text(json.dumps(results,indent=2));print('12 standalone protocol responses PASS')
