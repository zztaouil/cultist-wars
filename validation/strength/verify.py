"""Run existing assertions and sanitizers against isolated candidates."""
import subprocess,tempfile
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parents[1]
with tempfile.TemporaryDirectory() as td:
 t=Path(td)
 for name in ['q-lazy-eval','eval-hp','eval-race']:
  code=(R/'tests/test_bot.c').read_text().replace('#include "../bot.c"',f'#include "{D/(name+".c")}"')
  p=t/(name+'.c');p.write_text(code)
  subprocess.run(['cc','-O1','-g','-std=c11','-Wall','-Wextra','-fsanitize=address,undefined',str(p),'-o',str(t/name)],check=True)
  subprocess.run([str(t/name)],check=True);print(name+' sanitizer passed',flush=True)
 # Side-aware experimental evaluation must return the same value warm or cold.
 p=t/'cache.c';p.write_text('#define BOT_TEST\n#include "'+str(D/'eval-race.c')+'"\n#include <assert.h>\nint main(){initialize();State s={0};s.remaining=50;s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};s.u[2]=(Unit){41,10,2,0};prepare(&s);int a=evaluate(&s);s.side=1;prepare(&s);int b=evaluate(&s);memset(eval_tt,0,sizeof eval_tt);assert(b==evaluate(&s));s.side=0;prepare(&s);assert(a==evaluate(&s));return 0;}')
 subprocess.run(['cc','-O2','-std=c11',str(p),'-o',str(t/'cache')],check=True);subprocess.run([str(t/'cache')],check=True)
