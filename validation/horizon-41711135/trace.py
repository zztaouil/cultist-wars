import ctypes as C,json,subprocess
from pathlib import Path
D=Path(__file__).resolve().parent
CODE=r'''
#define BOT_TEST
#include "baseline.c"
static State unpack(int *p){State s={0};for(int i=0;i<UNITS;i++)s.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];prepare(&s);return s;}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();use_futility=0;use_threat_q=1;}
void fresh(void){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);memset(history,0,sizeof history);nn_root=0;nodes=qnodes=0;stopped=0;deadline=now_ns()+5000000000ULL;turn_deadline=deadline;}
int solve(int *p,int depth,int qleft,int *out,int *ok){State s=unpack(p);fresh();Action a={-1};int v;
if(depth)v=search(&s,depth,-INF,INF,&a);
else {v=quiescence(&s,-INF,INF,qleft);int tv;
if(qleft&&!terminal(&s,&tv)) {int unstable=s.remaining<=qleft||material_threat(&s);int best=unstable?-INF:evaluate_live(&s);Action aa[ACTIONS];int n=generate_mode(&s,aa,!unstable);for(int i=0;i<n;i++){State c=apply(&s,aa[i]);int z=-quiescence(&c,-INF,INF,qleft-1);if(z>best){best=z;a=aa[i];}}}}
memcpy(out,&a,sizeof a);*ok=!stopped;return v;}
void step(int *p,int *a){State s=unpack(p);s=apply(&s,*(Action*)a);for(int i=0;i<UNITS;i++){p[4*i]=s.u[i].pos;p[4*i+1]=s.u[i].hp;p[4*i+2]=s.u[i].owner;p[4*i+3]=s.u[i].leader;}p[56]=s.side;p[57]=s.remaining;p[58]=s.rng;p[59]=s.neutral_mask;p[60]=s.rng_known;}
'''
(D/'trace-engine.c').write_text(CODE)
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/'trace-engine.c'),'-o',str(D/'trace-engine.so')],check=True)
lib=C.CDLL(str(D/'trace-engine.so'));A=C.c_int*62;M=C.c_int*4
p=max((x for x in json.loads((D/'fixtures.json').read_text()) if x['game']=='921365647'),key=lambda x:x['turn'])
def cmd(a):
 k,i,t=a;return 'STOP' if k<0 else 'WAIT' if not k else f'{i} MOVE {t%13} {t//13}' if k==1 else f'{i} '+('SHOOT ' if k==2 else 'CONVERT ')+str(t)
def delta(s,n):
 return [dict(unit=i,before=s[4*i:4*i+4],after=n[4*i:4*i+4]) for i in range(14) if s[4*i+1:4*i+3]!=n[4*i+1:4*i+3]]
rows=[]
for turn in (17,19,21,23):
 for depth in (6,8):
  for initial in ([(3,1,8),(1,11,87),(1,1,60)] if turn==19 else [None]):
   s=A(*p['states'][turn]);lib.setup((C.c_int*91)(*p['walls']));line=[]
   for ply in range(depth+4):
    a=M();ok=C.c_int();v=lib.solve(s,max(0,depth-ply),min(4,depth+4-ply),a,C.byref(ok)) if ply or initial is None else (None)
    if not ply and initial is not None:a=M(*initial,0);ok.value=1
    action=list(a)[:3]
    if not ok.value or a[0]<0:line.append(dict(ply=ply,value=v,complete=bool(ok.value),stand_pat=a[0]<0));break
    before=list(s);lib.step(s,a);line.append(dict(ply=ply,side=before[56],action=action,command=cmd(action),value=v,changes=delta(before,list(s))))
   row=dict(turn=turn,depth=depth,initial=initial,line=line);rows.append(row);print(json.dumps(row),flush=True)
(D/'traces.json').write_text(json.dumps(rows,indent=2))
