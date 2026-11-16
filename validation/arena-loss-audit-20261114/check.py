import sys,json,ctypes as C,subprocess,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;root=D.parent.parent
sys.path.insert(0,str(root/'validation/arena-study/scripts'));import harness as h
src=root/'submissions/41714430.c';(D/'engine.c').write_text(h.CODE.replace('SOURCE',str(src)))
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/'engine.c'),'-o',str(D/'engine.so')],check=True)
l=C.CDLL(str(D/'engine.so'));n=0
for g in json.loads((D/'reconstructed.json').read_text()):
 h.setup(l,g['walls'])
 for s,z,a in zip(g['states'],g['states'][1:],g['acts']):
  got=h.step(l,s,a);assert got[:61]==z[:61],(g['id'],n);n+=1
print('C/Java transitions matched:',n);print('submission sha256:',hashlib.sha256(src.read_bytes()).hexdigest());print('working source equal:',src.read_bytes()==(root/'bot.c').read_bytes())
