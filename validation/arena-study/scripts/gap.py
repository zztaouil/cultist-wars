import json,sys,collections as C,statistics as S
import harness as h
lib=h.build('bot.c','gap')
R=[json.loads(l) for l in open('data/agree.jsonl') if '"hexagonee"' not in l]
part,np_=int(sys.argv[1]),int(sys.argv[2])
out=open(f'data/gap{part}.jsonl','w')
for r in R[part::np_]:
  if r['theirs']==r['ours']: continue
  h.setup(lib,r['walls']);s=r['state'];side=s[56]
  rows={}
  for who in ('theirs','ours'):
    a=r[who];c=h.step(lib,s,a)
    dmg=sum(max(0,s[4*i+1]-max(0,c[4*i+1])) for i in range(14) if s[4*i+2]!=side)
    owndmg=sum(max(0,s[4*i+1]-max(0,c[4*i+1])) for i in range(14) if s[4*i+2]==side)
    v=h.child(lib,s,a,5)
    rows[who]=dict(dmg=dmg,owndmg=owndmg,v5=v)
  out.write(json.dumps(dict(player=r['player'],turn=r['turn'],res=r['res'],theirs=r['theirs'],ours=r['ours'],t=rows['theirs'],o=rows['ours'],leader_t=s[4*r['theirs'][1]+3] if r['theirs'][0] else 0,leader_o=s[4*r['ours'][1]+3] if r['ours'][0] else 0))+'\n');out.flush()
