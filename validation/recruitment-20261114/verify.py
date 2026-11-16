import ctypes as C,json,re,subprocess,sys
from pathlib import Path
D=Path(__file__).resolve().parent;root=D.parents[1]
sys.path.insert(0,str(root/'validation/arena-verify/scripts'));import harness as h
sources=[D/'baseline.c',D/'selected.c']
def blob(src):
 s=src.read_text();a=s.index('static const char nn_blob[]=');b=s.index(';',a)
 return ''.join(re.findall(r'"([^"]*)"',s[a:b]))
assert blob(sources[0])==blob(sources[1]);assert sources[1].stat().st_size<=100000
libs=[]
for i,src in enumerate(sources):
 c=D/f'parity-{i}.c';c.write_text(h.CODE.replace('SOURCE',str(src)))
 so=D/f'parity-{i}.so';subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC','-DRECRUIT_WEIGHT=0',str(c),'-o',str(so)],check=True);libs.append(C.CDLL(str(so)))
games=json.loads((D.parent/'arena-loss-audit-20261114/reconstructed.json').read_text());n=0
for g in games[:12]:
 for lib in libs:h.setup(lib,g['walls'])
 for t in [20,40]:
  s=g['states'][min(t,len(g['states'])-1)];a,b=[h.run(lib,s,depth=3) for lib in libs]
  assert a['value']==b['value'] and a['action']==b['action'] and a['nodes']==b['nodes'],(g['id'],t,a,b);n+=1
print('NN bytes identical; submission size valid; disabled-feature fixed-depth parity:',n,'positions')
