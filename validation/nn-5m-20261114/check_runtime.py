"""Protocol, tree sanitizer, and differential checks against isolated sources."""
import json,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parent;root=R.parents[1];B=R/'build';results={}
protocol=(root/'tests/test_protocol.py').read_text()
for name in ['candidate','wide','tree']:
 binary=B/name
 subprocess.run(['cc','-O3','-std=c11',str(R/f'{name}.c'),'-o',str(binary)],check=True)
 code=protocol.replace("'./bot'",repr(str(binary)))
 p=subprocess.run([sys.executable,'-c',code],text=True,capture_output=True,check=True);results[name]=p.stdout.strip();print(name,p.stdout.strip(),flush=True)
test=(root/'tests/test_bot.c').read_text().replace('#include "../bot.c"','#include "../tree.c"');(B/'tree-test.c').write_text(test)
subprocess.run(['cc','-O1','-g','-std=c11','-fsanitize=address,undefined',str(B/'tree-test.c'),'-o',str(B/'tree-sanitize')],check=True)
subprocess.run([str(B/'tree-sanitize')],check=True)
source=root/'tests/test_differential.py';code=source.read_text().replace("ROOT / 'bot.c'",f'Path({str(R/"tree.c")!r})')
p=subprocess.run([sys.executable,'-c',f'__file__={str(source)!r}\n'+code],text=True,capture_output=True,check=True);results['differential']=p.stdout.strip();print(p.stdout,flush=True)
(R/'runtime.json').write_text(json.dumps(results,indent=2))
