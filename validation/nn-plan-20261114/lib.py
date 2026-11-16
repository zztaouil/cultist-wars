import sys,ctypes as C,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'validation/arena-verify/scripts'))
import harness as h
EXT=(Path(__file__).parent/'ext.h').read_text()
def build(src,name,budget,flags=()):
    d=ROOT/'build';d.mkdir(exist_ok=True);c=d/(name+'.cpp')
    code=h.CODE.replace('SOURCE',str((ROOT/src).resolve()))
    code=code.replace('(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]}','Unit{(uint8_t)p[4*i],(uint8_t)p[4*i+1],(uint8_t)p[4*i+2],(uint8_t)p[4*i+3]}')
    for sg in ('void setup','int run','int score_child','void step','int static_eval'):code=code.replace(sg,'extern "C" '+sg)
    c.write_text(code+EXT);so=d/(name+'.so')
    subprocess.run(['c++','-O3','-march=native','-std=c++17','-shared','-fPIC',f'-DBUDGET_MS={budget}',*['-D'+f for f in flags],str(c),'-o',str(so)],check=True)
    L=C.CDLL(str(so));L.eval_terms.argtypes=[C.c_void_p,C.c_void_p];return L
A62=C.c_int*62
def actions(L,s):
    out=(C.c_int*(3*300))();n=L.gen_actions(A62(*s),out);return [list(out[3*i:3*i+3]) for i in range(n)]
def terms(L,s):
    t=(C.c_double*8)();L.eval_terms(A62(*s),t);return list(t)
def threat(L,s):return L.is_threat(A62(*s))
