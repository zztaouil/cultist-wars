import subprocess,json,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;root=D.parents[1]
text=(root/'tests/test_bot.c').read_text();results=[]
for name in ('baseline','contest','extension','combined'):
 src=D/(name+'.c');test=D/('test-'+name+'.c');test.write_text(text.replace('#include "../bot.c"','#include "'+str(src)+'"'))
 for san in (False,True):
  exe=D/('test-'+name+('-san' if san else ''))
  cmd=['cc','-std=c11','-O1' if san else '-O3']+(['-g','-fsanitize=address,undefined'] if san else [])+[str(test),'-o',str(exe)]
  subprocess.run(cmd,check=True);p=subprocess.run([str(exe)],capture_output=True,text=True)
  results.append(dict(variant=name,sanitized=san,returncode=p.returncode,output=(p.stdout+p.stderr)[-2000:]))
  (D/'checks.json').write_text(json.dumps(results,indent=2));print(name,san,p.returncode,flush=True)
  if p.returncode:raise RuntimeError(results[-1])
 subprocess.run(['cc','-O3','-std=c11',str(src),'-o',str(D/name)],check=True)
