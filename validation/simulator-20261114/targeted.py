from experiment import *
for name in sys.argv[1:] or ['baseline','neutral','ray','metadata','packed','undo']:
 s=(D/'targeted-template.c').read_text().replace('WRAPPER',str(D/(name+'-wrap.c')))
 s=s.replace('UNDO_CHECK','State copy=s;Undo u;make(&copy,actions[i],&u);assert(!memcmp(&copy,&t,sizeof t));unmake(&copy,&u);assert(!memcmp(&copy,&s,sizeof s));' if name=='undo' else '')
 s=s.replace('META_CHECK','Action manual=actions[i];manual.hit=0;State ref=apply(&s,manual);assert(!memcmp(&ref,&t,sizeof t));assert(same(manual,actions[i]));assert(qmove(manual)==qmove(actions[i]));' if name in ['metadata','packed'] else '')
 s=s.replace('TRACKER_CHECK','''memset(wall,0,sizeof wall);initialize();memset(&s,0,sizeof s);s.remaining=150;
s.u[2]=(Unit){0,10,0,0};s.u[3]=(Unit){3,10,1,0};s.u[4]=(Unit){1,10,2,0};prepare(&s);
Tracker tracker={0};tracker.n=2;tracker.states[0]=s;tracker.states[1]=s;tracker.states[1].u[4].pos=13;prepare(&tracker.states[1]);
Action aim={.kind=SHOOT,.unit=2,.target=3,.hit=5},manual=aim;manual.hit=0;
State want[2]={apply(&tracker.states[0],manual),apply(&tracker.states[1],manual)};track_action(&tracker,aim);
assert(!memcmp(want,tracker.states,sizeof want));''' if name in ['metadata','packed'] else '')
 p=D/(name+'-targeted.c');p.write_text(s);exe=D/(name+'-targeted')
 subprocess.run(['cc','-O1','-g','-std=c11','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True);print(name,'passed',flush=True)
