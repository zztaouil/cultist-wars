import ctypes as C,json,subprocess,sys,statistics,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parents[1]
sys.path.insert(0,str(R/'validation/arena-verify/scripts'));import harness as h
src=(R/'submissions/41714601.c').read_text()
variants={'baseline':src,'guard_futility':src.replace('&& s->remaining > 4 && abs(alpha) < WIN / 2;','&& s->remaining > 4 && abs(alpha) < WIN / 2 && !material_threat(s);'),'no_nn':'#define NN_ENABLE 0\n'+src}
G=json.loads((R/'validation/losses-41714601/reconstructed.json').read_text())
positions=[]
for gid,turns in [(921574605,[79,81,83,85,89,91]),(921574271,[29,39,49]),(921574680,[33,37,99,139]),(921572974,[52,100,140]),(921574110,[115,117]),(921574564,[22,24])]:
 g=next(g for g in G if g['id']==f'game-{gid}')
 for t in turns:positions.append((g,t))
libs={}
for name,s in variants.items():
 p=D/(name+'.c');p.write_text(s)
 wrapper=h.CODE.replace('SOURCE',str(p))+'''\nvoid configure(int f){use_futility=f;}\nvoid init_cost(const int*p,uint64_t*out){State s=unpack(p);prepare(&s);uint64_t a=now_ns();nn_begin(&s);*out=now_ns()-a;nn_root=0;}\n'''
 c=D/(name+'_wrap.c');c.write_text(wrapper);so=D/(name+'.so')
 subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(c),'-o',str(so)],check=True);libs[name]=C.CDLL(str(so))
rows=[]
for rep in range(3):
 for g,t in positions:
  for name in list(libs)[::1 if rep%2==0 else -1]:
   lib=libs[name];h.setup(lib,g['walls']);lib.configure(1)
   z=h.run(lib,g['states'][t]);rows.append(dict(rep=rep,id=g['id'],turn=t,variant=name,**z))
(D/'probes.json').write_text(json.dumps(rows,indent=2))
summary={}
for name in libs:
 rr=[r for r in rows if r['variant']==name];summary[name]={'mean_depth':statistics.mean(r['depth'] for r in rr),'mean_nodes':statistics.mean(r['nodes'] for r in rr),'max_ms':max(r['ns'] for r in rr)/1e6,'bytes':len(variants[name].encode())}
base={(r['rep'],r['id'],r['turn']):r for r in rows if r['variant']=='baseline'}
for name in libs:
 summary[name]['changed_actions']=sum(r['action']!=base[r['rep'],r['id'],r['turn']]['action'] for r in rows if r['variant']==name)
# Compare pruning on/off at identical fixed depth, no policy ordering at either arm.
parity=[]
for g,t in positions:
 results=[]
 for f in [0,1]:
  lib=libs['baseline'];h.setup(lib,g['walls']);lib.configure(f);results.append(h.run(lib,g['states'][t],depth=4))
 parity.append(dict(id=g['id'],turn=t,unpruned=results[0],pruned=results[1]))
(D/'futility-depth4.json').write_text(json.dumps(parity,indent=2))
summary['futility_depth4_value_differences']=sum(p['pruned']['value']!=p['unpruned']['value'] for p in parity)
summary['baseline_sha256']=hashlib.sha256(src.encode()).hexdigest()
(D/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
