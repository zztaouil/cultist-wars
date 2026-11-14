import json,gzip,os,collections as C
R=os.environ['HOME']+'/mnt/cultist-wars/validation/arena-study/results/'
LB=json.load(open(R+'lb.json'))['users']
CUR={u['agentId']:u['pseudo'] for u in LB}
def games():
  meta={str(g['id']):g for g in map(json.loads,gzip.open(R+'games-2026-11-13.jsonl.gz','rt'))}
  cur=None
  for line in open('recon.txt'):
    t=line[0]
    if t=='G':
      _,gid,w=line.split();cur=dict(id=gid,walls=[int(c) for c in w],states=[],actions=[],meta=meta[gid])
    elif t=='S':
      s,a=json.loads('['+line[2:]+']');cur['states'].append(s);cur['actions'].append(a)
    elif t=='E': yield cur
def counts(s): return [sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1)]
