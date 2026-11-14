import json,glob
def games():
  meta={}
  for fn in glob.glob('data/games/*.json'):
    g=json.load(open(fn));meta[str(g['gameId'])]=g
  cur=None
  for line in open('data/recon.txt'):
    t=line[0]
    if t=='G':
      _,gid,w=line.split();cur=dict(id=gid,walls=[int(c) for c in w],states=[],actions=[],meta=meta[gid])
    elif t=='S':
      s,a=json.loads('['+line[2:]+']');cur['states'].append(s);cur['actions'].append(a)
    elif t=='E': yield cur
def counts(s): return [sum(s[4*i+1]>0 and s[4*i+2]==p for i in range(14)) for p in (0,1)]
