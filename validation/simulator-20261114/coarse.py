from experiment import *
s,names=profile_src()
for name in ['fast_hit','move_neutral']:
 start=s.index('static '+('int' if name=='fast_hit' else 'void')+' '+name+'_impl(')
 end=s.index('\nstatic '+('int' if name=='fast_hit' else 'void')+' '+name+'_impl(',start+1)
 s=s[:start]+s[end+1:]
 s=s.replace(name+'_impl(',name+'(')
s=s.replace('static int fast_hit(', 'static uint64_t ray_calls,ray_multi,ray_cells;\nstatic int fast_hit(',1)
s=s.replace('Bits r = ray_bits[from][to];','++ray_calls;\nBits r = ray_bits[from][to];')
s=s.replace('const Ray *path = &rays[from][to];','++ray_multi;\nconst Ray *path = &rays[from][to];')
s=s.replace('p = path->cell[k];','ray_cells += k+1;\np = path->cell[k];')
s+='\nvoid ray_stats(uint64_t*out){out[0]=ray_calls;out[1]=ray_multi;out[2]=ray_cells;}\n'
lib=build('coarse-profile',s,True)
for p in json.loads((D/'positions.json').read_text()):run(lib,p)
t=T();lib.profile_stats(t);r=T();lib.ray_stats(r)
result={n:dict(ns=t[i],calls=t[12+i]) for i,n in enumerate(names) if n not in ['fast_hit','move_neutral']}
result['ray']=dict(calls=r[0],multi=r[1],multi_cells=r[2]);result['note']='Exclusive coarse regions, fast_hit included in caller; neutral included in apply. Instrumented overhead remains; uninstrumented benchmark determines promotion.'
(D/'coarse-profile.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
