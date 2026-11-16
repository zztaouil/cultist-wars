"""Cached policy ordering at first opponent ply, depth >= 3; no value bias."""
from pathlib import Path
import sys
R=Path(__file__).resolve().parent
source=sys.argv[1] if len(sys.argv)>1 else 'selected.c'
out=sys.argv[2] if len(sys.argv)>2 else 'tree.c'
s=(R/source).read_text()
s=s.replace('static void nn_begin(const State *s){', 'static struct{uint64_t key;int valid,r[ACTIONS];} nc[64];\nstatic void nn_begin(const State *s){memset(nc,0,sizeof nc);')
start=s.index('static int search(');prefix=s[:start];search=s[start:]
needle='    Action actions[ACTIONS]; int n = generate(s, actions);'
assert search.count(needle)==1
search=search.replace(needle,needle+'''
    int ci=-1;if(NN_ENABLE&&nn_root&&nn_root->remaining-s->remaining==1&&depth>=3){ci=key&63;if(!nc[ci].valid||nc[ci].key!=key){nc[ci].valid=1;nc[ci].key=key;for(int j=0;j<n;j++){if(now_ns()>=deadline){stopped=1;return 0;}float f[NF];features(s,actions[j],f);nc[ci].r[j]=(int)(nn_score(f)*1024);}}}''')
needle='        if (s == nn_root) actions[i].order = nn_order(a);'
assert search.count(needle)==1
search=search.replace(needle,needle+'\n        else if(ci>=0)actions[i].order=nc[ci].r[i];')
s=prefix+search
assert len(s.encode())<100000,len(s.encode())
(R/out).write_text(s)
print('tree source bytes',len(s.encode()))
