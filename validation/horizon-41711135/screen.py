import ctypes as C,json,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent
CODE=r'''
#define BOT_TEST
#include "SOURCE"
static Tracker tr;static int first;static State obs;
static State unpack(const int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];return s;}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tr,0,sizeof tr);first=1;}
int observe(int *p){obs=unpack(p);obs.rng=0;obs.rng_known=0;obs.neutral_mask=0;track_observation(&tr,&obs,first);first=0;p[58]=obs.rng;p[59]=obs.neutral_mask;p[60]=obs.rng_known;return tr.n;}
int history_size(void){return sizeof history;}
void history_get(void *p){memcpy(p,history,sizeof history);}
void history_set(void *p){memcpy(history,p,sizeof history);memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(eval_tt,0,sizeof eval_tt);}
void actual(int *a){track_action(&tr,*(Action*)a);}
int probe(int *p,int depth,int ms,int *out,uint64_t *st){State s=unpack(p);prepare(&s);uint64_t start=now_ns();turn_deadline=start+(uint64_t)ms*1000000ULL;deadline=turn_deadline;nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;Action a={0};int v,d=depth;
if(depth){nn_begin(&s);v=search(&s,depth,-INF,INF,&a);nn_root=0;}else a=choose(&s,&d,&v);
memcpy(out,&a,sizeof a);st[0]=d;st[1]=nodes;st[2]=qnodes;st[3]=now_ns()-start;st[4]=stopped;return v;}
'''
A=C.c_int*62;M=C.c_int*4
def build(name,source=None,flags=()):
 c=D/('screen-'+name+'.c');c.write_text(CODE.replace('SOURCE',str(D/((source or name)+'.c'))));so=c.with_suffix('.so')
 subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',*[f'-D{x}' for x in flags],str(c),'-o',str(so)],check=True);return C.CDLL(str(so))
def run(lib,s,depth=0,ms=49):
 a=M();st=(C.c_uint64*5)();v=lib.probe(A(*s),depth,ms,a,st);return dict(action=list(a)[:3],value=v,depth=st[0],nodes=st[1],qnodes=st[2],ms=st[3]/1e6,stopped=bool(st[4]))
def main():
 libs={k:build(k) for k in ('baseline','contest','extension','combined')};prefix=build('prefix','baseline');off=build('ablated','combined',('CONTEST_EVAL=0','THREAT_EXTENSION=0'))
 fixtures=json.loads((D/'fixtures.json').read_text());groups={}
 for p in fixtures:groups.setdefault(p['game'],[]).append(p)
 hist=C.create_string_buffer(prefix.history_size());rows=[];parity=[]
 for gid,pp in groups.items():
  long=max(pp,key=lambda p:p['turn']);w=(C.c_int*91)(*long['walls'])
  for lib in [prefix,*libs.values(),off]:lib.setup(w)
  selected={p['turn']:p for p in pp}
  for t in range(1,long['turn']+1,2):
   s=A(*long['states'][t]);rng=prefix.observe(s)
   if t in selected:
    prefix.history_get(hist);r={}
    for name,lib in libs.items():
     runs=[]
     for depth,ms in [(4,2000),(0,49),(0,500)]:
      lib.history_set(hist);runs.append(run(lib,s,depth,ms))
     r[name]=dict(fixed=runs[0],timed=runs[1],deep=runs[2])
    off.history_set(hist);ab=run(off,s,4,2000);ref=r['baseline']['fixed'];assert all(ab[k]==ref[k] for k in ('action','value','nodes','qnodes','stopped')),(gid,t,ab,ref);parity.append([gid,t])
    row=dict(game=gid,turn=t,kind=selected[t]['kind'],rng=rng,actual=long['actions'][t],results=r);rows.append(row);(D/'screen.json').write_text(json.dumps(rows,indent=2));print(gid,t,flush=True)
   run(prefix,s);prefix.actual(M(*long['actions'][t],0))
 (D/'ablation-parity.json').write_text(json.dumps(dict(positions=len(parity),matches=True)))
if __name__=='__main__':main()
