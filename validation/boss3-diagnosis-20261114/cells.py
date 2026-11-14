"""Outcome following boss decisions, grouped by (boss class, our-engine class) confusion cell. Pooled t=6..112."""
import json
from collections import defaultdict
G={g['id']:g for g in json.load(open('boss_games.json'))}
R={}
for f in ('bosspos.jsonl','bosspos-mid.jsonl','bosspos-late.jsonl'):
    for l in open(f):o=json.loads(l);R[(o['id'],o['t'])]=o
R=list(R.values())
def cls(a,seat):return ['WAIT','CMOVE','SHOOT','CONVERT'][a[0]] if not(a[0]==1 and a[1]==seat) else 'LMOVE'
def cnt(s,p):return sum(1 for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def hp(s,p):return sum(s[4*i+1] for i in range(14) if s[4*i+1]>0 and s[4*i+2]==p)
def d(r,k,f):
    g=G[r['id']];S=g['states'];b=r['boss_seat'];t=r['t'];u=min(t+k,len(S)-1);return (f(S[u],b)-f(S[u],1-b))-(f(S[t],b)-f(S[t],1-b))
cells=defaultdict(list)
for r in R:
    if r['v_boss'] is None or abs(r['v_best'])>500000:continue
    same=tuple(r['boss'])==tuple(r['ours']);cells[('AGREE' if same else cls(r['boss'],r['boss_seat'])+'>'+cls(r['ours'],r['boss_seat']))].append(r)
print('%-16s %4s %7s | boss count-diff change +12 +24 +40 plies | hp-diff change +12 | final margin(boss)'%('boss>ours','n','medgap'))
for k,X in sorted(cells.items(),key=lambda x:-len(x[1])):
    if len(X)<20:continue
    gaps=sorted(r['v_best']-r['v_boss'] for r in X)
    fm=[(lambda g,b:g['final'][b]-g['final'][1-b])(G[r['id']],r['boss_seat']) for r in X]
    print('%-16s %4d %7d | %+.2f %+.2f %+.2f | %+.1f | %+.2f'%(k,len(X),gaps[len(gaps)//2],*[sum(d(r,j,cnt) for r in X)/len(X) for j in (12,24,40)],sum(d(r,12,hp) for r in X)/len(X),sum(fm)/len(fm)))
tot=len(R);sh=sum(cls(r['boss'],r['boss_seat'])=='SHOOT' for r in R);so=sum(cls(r['ours'],r['boss_seat'])=='SHOOT' for r in R)
print('shoot share in identical positions: boss %.1f%%, our engine %.1f%% (n=%d)'%(100*sh/tot,100*so/tot,tot))
for c in ('LMOVE','CMOVE','CONVERT','WAIT'):print(c,'boss %.1f%% ours %.1f%%'%(100*sum(cls(r['boss'],r['boss_seat'])==c for r in R)/tot,100*sum(cls(r['ours'],r['boss_seat'])==c for r in R)/tot))
