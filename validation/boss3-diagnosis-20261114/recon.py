"""Reconstruct boss games with unmodified compiled Java model (validation/losses-41709358/java). Tag version."""
import json,subprocess,re
from pathlib import Path
D=Path(__file__).resolve().parent;V=D.parent;BOSS=4611894
d=json.load(open(D/'all_games.json'))
AG={6880547:'41709358',6881114:'41710268',6881442:'41711135'}
def version(g):
    s=g['src']
    if g['kind'] in('arena','arena-log'):
        for a in g['agents'].values():
            if a[2]:return AG.get(a[1],'agent%s'%a[1])
        return None
    b=s.rsplit('/',1)[1]
    if s.startswith('race/boss/base-'):return 'cd28(=41710268)'
    if s.startswith('boss-41709358'):return '41709358'
    if s.startswith('nn-41710886/boss5'):return '41711135'
    if s.startswith('horizon') and 'baseline' in b:return '41711135'
    if s.startswith('nn-large') and 'baseline' in b:return '41711135'
    if s.startswith('strength/boss-baseline') or s.startswith('lmr/boss-baseline'):return '41709358'
    if s.startswith('tuning/boss-baseline'):return 'ce73(41709358+lazyQ)'
    if s.startswith('tuning/boss-tuned'):return 'cd28(=41710268)'
    return 'variant:'+s.split('/')[0]+':'+re.sub(r'-?\d{4,}','',b).replace('.json','').strip('-')
out=[];seen=set()
for g in d['games']:
    ids={int(k):v for k,v in g['agents'].items()}
    if not any(a[1]==BOSS for a in ids.values()):continue
    if g['id'] in seen:continue
    seen.add(g['id'])
    bs=[k for k,a in ids.items() if a[1]==BOSS][0]
    g['boss_seat']=bs;g['opp']=ids[1-bs][0] if 1-bs in ids else '?';g['me']=bool(ids.get(1-bs,[0,0,0])[2]);g['version']=version(g) if g['me'] else 'other:'+g['opp']
    out.append(g)
inp=[]
for g in out:
    acts=[a if a else 'WAIT' for a in g['actions']]
    g['actions']=acts;inp.append('G %s %s %d'%(g['id'],g['seed'],len(acts)));inp+=acts
r=subprocess.run(['java','-cp',str(D/'model-j11.jar'),'model.Batch'],input='\n'.join(inp)+'\n',capture_output=True,text=True)
cur=None;rec={}
for line in r.stdout.splitlines():
    if line.startswith('G '):_,gid,w=line.split();cur=dict(walls=w,states=[],acts=[],bad=None);rec[gid]=cur
    elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');cur['states'].append(s);cur['acts'].append(a)
    elif line.startswith('X '):cur['bad']=line
for g in out:
    c=rec[g['id']];g.update(walls=c['walls'],states=c['states'],acts=c['acts'],bad=c['bad'])
    f=c['states'][-1];cnt=[sum(1 for i in range(14) if f[4*i+1]>0 and f[4*i+2]==p) for p in (0,1)]
    g['final']=cnt
    sc=g.get('scores');g['score_ok']=(sc is not None and [int(float(x)) for x in sc]==cnt)
json.dump(out,open(D/'boss_games.json','w'))
from collections import Counter
c=Counter()
for g in out:
    my=1-g['boss_seat'];r='W' if g['final'][my]>g['final'][1-my] else 'L' if g['final'][my]<g['final'][1-my] else 'D'
    g['res']=r
    c[(g['version'],'seat%d'%my,r)]+=1
vs=sorted({k[:2] for k in c})
for v in vs:print(v,{r:c[v+(r,)] for r in 'WDL'})
print(len(out),'bad',[ (g['id'],g['src'],g['bad']) for g in out if g['bad']][:10])
print('score mismatch',[(g['id'],g['src'],g.get('scores'),g['final'],len(g['actions'])) for g in out if not g['score_ok']][:15])
