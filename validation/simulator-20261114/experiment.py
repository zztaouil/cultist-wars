import ctypes as C, json, subprocess, statistics, re, hashlib, sys
from pathlib import Path
D=Path(__file__).resolve().parent; R=D.parents[1]
BASE=(D/'baseline.c').read_text()
A=C.c_int*62; W=C.c_int*91; O=C.c_int*4; T=C.c_uint64*32

def corpus():
 g=json.loads((R/'validation/losses-41714601/reconstructed.json').read_text())
 out=[]
 for game in g:
  for t in [0,20,50,80,110,140]:
   if t<len(game['states']):out.append(dict(id=game['id'],turn=t,walls=game['walls'],state=game['states'][t]))
 return out
WRAP=r'''
#define BOT_TEST
#include "SOURCE"
#include <assert.h>
static volatile uint64_t sink;
static State unpack(const int*p){State s={0};for(int i=0;i<14;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
static void pack(State s,int*p){for(int i=0;i<14;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;p[61]=0;}
static void check(State s){State f=s;prepare(&f);assert(s.key==hash_state(&s));assert(s.key==f.key&&s.metadata==f.metadata&&s.leaders==f.leaders);assert(!memcmp(s.owned,f.owned,sizeof s.owned));assert(!memcmp(s.occupants,f.occupants,sizeof s.occupants));assert(!memcmp(&s.occupied,&f.occupied,sizeof s.occupied));}
void setup(const int*w){memcpy(wall,w,sizeof wall);initialize();nn_init();}
int solve(const int*p,int depth,int*out,uint64_t*stats){State s=unpack(p);Action a={0};int v,done=depth;nodes=qnodes=hits=cutoffs=pruned=0;stopped=0;turn_deadline=0;deadline=now_ns()+60000000000ULL;uint64_t start=now_ns();if(depth){nn_begin(&s);v=search(&s,depth,-INF,INF,&a);nn_root=0;}else a=choose(&s,&done,&v);stats[0]=now_ns()-start;stats[1]=nodes;stats[2]=qnodes;stats[3]=hits;stats[4]=cutoffs;stats[5]=pruned;stats[6]=stopped;stats[7]=done;out[0]=a.kind;out[1]=a.unit;out[2]=a.target;return v;}
int actions(const int*p,int*out){State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);for(int i=0;i<n;i++){out[4*i]=a[i].kind;out[4*i+1]=a[i].unit;out[4*i+2]=a[i].target;out[4*i+3]=a[i].order;}return n;}
void transition(const int*p,int index,int prepared,int*out){State s=unpack(p);if(!prepared)s.ready=0;Action a[ACTIONS];generate(&s,a);State t=apply(&s,a[index]);if(prepared)check(t);pack(t,out);}
void micro(const int*p,int reps,uint64_t*out){State s=unpack(p);Action a[ACTIONS];int n=generate(&s,a);uint64_t start=now_ns(),sum=0;for(int k=0;k<reps;k++)for(int i=0;i<n;i++){State t=apply(&s,a[i]);sum+=t.key+t.rng;}sink=sum;out[0]=now_ns()-start;out[1]=(uint64_t)reps*n;out[2]=sizeof(State);out[3]=sizeof(Action);out[4]=sizeof(Entry);}
PROFILE_EXPORT
'''

def build(name,src=None,profile=False):
 if src is not None:(D/(name+'.c')).write_text(src)
 code=WRAP.replace('SOURCE',str(D/(name+'.c'))).replace('PROFILE_EXPORT','void profile_stats(uint64_t*out){memcpy(out,prof_ns,sizeof prof_ns);memcpy(out+12,prof_count,sizeof prof_count);}' if profile else '')
 (D/(name+'-wrap.c')).write_text(code)
 subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/(name+'-wrap.c')),'-o',str(D/(name+'.so'))],check=True)
 return C.CDLL(str(D/(name+'.so')))

def profile_src():
 s=BASE
 specs=[('apply','State','const State *s, Action a','s,a'),('generate_mode','int','const State *s, Action actions[ACTIONS], int tactical','s,actions,tactical'),('evaluate_live','int','const State *s','s'),('nn_begin','void','const State *s','s'),('fast_hit','int','const State *s, int from, int to','s,from,to'),('move_neutral','void','State *s, int corpse','s,corpse'),('search','int','const State *s, int depth, int alpha, int beta, Action *chosen','s,depth,alpha,beta,chosen'),('quiescence','int','const State *s, int alpha, int beta, int left','s,alpha,beta,left'),('material_threat','int','const State *s','s')]
 s=s.replace('static int manhattan', 'static uint64_t prof_ns[12],prof_count[12],prof_last;static int prof_active=11;\nstatic int manhattan',1)
 for idx,(name,ret,args,call) in enumerate(specs):
  pat=r'static '+ret+r' '+name+r'\([^)]*\)\s*\{';m=re.search(pat,s);assert m,name
  prefix=f'static {ret} {name}_impl({args});\nstatic {ret} {name}({args}){{uint64_t t=now_ns();int prev=prof_active;prof_ns[prev]+=t-prof_last;prof_active={idx};prof_count[{idx}]++;prof_last=t;'
  prefix+=(f'{ret} v=' if ret!='void' else '')+f'{name}_impl({call});t=now_ns();prof_ns[{idx}]+=t-prof_last;prof_active=prev;prof_last=t;'+('return v;' if ret!='void' else '')+'}\n'
  s=s[:m.start()]+prefix+m.group().replace(name+'(',name+'_impl(')+s[m.end():]
 return s,[x[0] for x in specs]

def run(lib,p,depth=5):
 lib.setup(W(*p['walls']));o=O();t=T();v=lib.solve(A(*p['state']),depth,o,t)
 return dict(value=v,action=list(o)[:3],ns=t[0],visits=t[1],qnodes=t[2],hits=t[3],cutoffs=t[4],pruned=t[5],stopped=t[6],depth=t[7])

def holdout_corpus():
 ps=[]
 for f in ['validation/strength/replay-positions.json','validation/search-20261114/latest-positions.json']:
  ps+=json.loads((R/f).read_text())
 seen=set();out=[]
 for p in ps:
  key=str(p['walls'])+str(p['state'])
  if key not in seen:
   seen.add(key)
   if p.get('turn',-1) in [0,25,50,75,100,125,140] or 'game' not in p:out.append(p)
 return out

if __name__=='__main__':
 (D/'holdout-positions.json').write_text(json.dumps(holdout_corpus()))
 ps=corpus();(D/'positions.json').write_text(json.dumps(ps));print('positions',len(ps),flush=True)
 src,names=profile_src();lib=build('profile',src,True);rows=[]
 for p in ps: rows.append(run(lib,p))
 t=T();lib.profile_stats(t);result={n:dict(ns=t[i],calls=t[12+i]) for i,n in enumerate(names)}
 result['note']='Exclusive instrumented times; timestamp/wrapper overhead included. NN root initialization included. Search/Q residual includes ordering/cache/terminal. Not promotion timings.'
 (D/'profile.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
