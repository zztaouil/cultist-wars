"""Replay-history controlled diagnostic; frozen source, no production edits."""
import ctypes as C,json,subprocess,hashlib
from pathlib import Path
D=Path(__file__).resolve().parent;R=D.parent/'nn-41710886/arena'
src=(R/'41711135.c').read_text().replace('if(!NN_ENABLE)return;', 'if(!diag_nn)return;')
src=src.replace('static const State *nn_root;', 'static int diag_nn=1;\nstatic const State *nn_root;')
wrapper=r'''
static Tracker tr;static int first;
static State obs;
static unsigned char stt[sizeof tt],sqtt[sizeof qtt],sev[sizeof eval_tt],sh[sizeof history];
void save_context(void){memcpy(stt,tt,sizeof tt);memcpy(sqtt,qtt,sizeof qtt);memcpy(sev,eval_tt,sizeof eval_tt);memcpy(sh,history,sizeof history);}
void restore_context(void){memcpy(tt,stt,sizeof tt);memcpy(qtt,sqtt,sizeof qtt);memcpy(eval_tt,sev,sizeof eval_tt);memcpy(history,sh,sizeof history);}
void setup(int *w){memcpy(wall,w,sizeof wall);initialize();memset(&tr,0,sizeof tr);first=1;}
int observe(int *p){memset(&obs,0,sizeof obs);for(int i=0;i<UNITS;i++)if(p[4*i+1])obs.u[i]=(Unit){p[4*i],p[4*i+1],p[4*i+2],p[4*i+3]};obs.side=p[56];obs.remaining=p[57];track_observation(&tr,&obs,first);first=0;save_context();return tr.n;}
void actual(int *a){track_action(&tr,*(Action*)a);}
int probe(int nn,int ms,int depth,int fut,int threat,int *a,uint64_t *m){
restore_context();diag_nn=nn;use_futility=fut;use_threat_q=threat;
/* Caches contain bounds from a different selective search when ablating. */
if(!fut||!threat){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);}
State s=obs;prepare(&s);uint64_t start=now_ns();turn_deadline=start+(uint64_t)ms*1000000;deadline=turn_deadline;
int d=depth,v;Action best={0};nodes=qnodes=cutoffs=hits=pruned=0;stopped=0;
if(depth){memset(tt,0,sizeof tt);memset(qtt,0,sizeof qtt);nn_begin(&s);v=search(&s,depth,-INF,INF,&best);nn_root=0;}else best=choose(&s,&d,&v);
memcpy(a,&best,sizeof best);m[0]=d;m[1]=nodes;m[2]=qnodes;m[3]=now_ns()-start;m[4]=stopped;return v;}
'''
(D/'engine.c').write_text('#define BOT_TEST\n'+src+wrapper)
subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(D/'engine.c'),'-o',str(D/'engine.so')],check=True)
lib=C.CDLL(str(D/'engine.so'));A=C.c_int*62;M=C.c_int*4
rows={x['id']:x for x in json.loads((R/'study78.json').read_text()) if x['seat']==1 and x['result']=='L' and x.get('verified')}
games={}
for line in (R/'recon78.txt').read_text().splitlines():
 if line.startswith('G '):_,gid,w=line.split();g={'walls':list(map(int,w)),'states':[],'actions':[]};games[gid]=g
 elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');g['states'].append(s);g['actions'].append(a)
selection={}
for gid,row in rows.items():
 g=games[gid];events={}
 for t,(s,n) in enumerate(zip(g['states'],g['states'][1:])):
  kinds=[]
  for i in range(14):
   if s[4*i+1] and s[4*i+2]==2 and n[4*i+2]==0:kinds.append('opponent_recruit')
   if s[4*i+1] and s[4*i+2]==1 and (not n[4*i+1] or n[4*i+2]==0):kinds.append('own_material_loss')
  for k in kinds:
   # Earliest event of each type, plus preceding own decision two plies earlier.
   if k not in events:
    p=t if t%2 else t-1
    if p>=1:events[k]=p
 sel={}
 for kind,t in events.items():
  for p in (max(1,t-2),t):sel.setdefault(p,[]).append(kind)
 selection[gid]=sel
(D/'selection.json').write_text(json.dumps(selection,indent=2))
def run(nn=1,ms=49,depth=0,fut=1,threat=1):
 a=M();m=(C.c_uint64*5)();v=lib.probe(nn,ms,depth,fut,threat,a,m)
 return dict(action=list(a)[:3],value=v,depth=m[0],nodes=m[1],qnodes=m[2],ms=m[3]/1e6,stopped=bool(m[4]))
with (D/'results.jsonl').open('w') as out:
 for gid,sel in selection.items():
  g=games[gid];lib.setup((C.c_int*91)(*g['walls']))
  for t in range(1,max(sel)+1,2):
   rng=lib.observe(A(*g['states'][t]))
   if t in sel:
    rr={}
    for name,args in [('nn49',(1,49)),('off49',(0,49)),('nn500',(1,500)),('off500',(0,500)),('nn_d4',(1,2000,4)),('off_d4',(0,2000,4)),('no_futility',(1,500,0,0)),('no_threat_q',(1,500,0,1,0))]:rr[name]=run(*args)
    item=dict(game=gid,turn=t,kinds=sel[t],opponent=rows[gid]['opponent'],actual=g['actions'][t],rng_candidates=rng,results=rr)
    out.write(json.dumps(item)+'\n');out.flush();print(gid,t,flush=True)
   # Advance shared history with baseline search; force actual arena move in tracker.
   run();lib.actual(M(*g['actions'][t],0))
(D/'manifest.json').write_text(json.dumps({'source_sha256':hashlib.sha256((R/'41711135.c').read_bytes()).hexdigest(),'games':len(selection),'positions':sum(map(len,selection.values()))},indent=2))
