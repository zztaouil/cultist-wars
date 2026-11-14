import json,collections
from pathlib import Path
D=Path(__file__).resolve().parent
rows=[json.loads(x) for x in (D/'results.jsonl').read_text().splitlines()]
out={'positions':len(rows),'games':len(set(x['game'] for x in rows)),'rng_zero':sum(x['rng_candidates']==0 for x in rows)}
for a,b in [('nn49','off49'),('nn49','nn500'),('nn500','off500'),('nn_d4','off_d4'),('nn500','no_futility'),('nn500','no_threat_q')]:
 pairs=[(x['results'][a],x['results'][b]) for x in rows]
 out[a+'_vs_'+b]={'action_changes':sum(x['action']!=y['action'] for x,y in pairs),'value_changes':sum(x['value']!=y['value'] for x,y in pairs),'deeper':sum(y['depth']>x['depth'] for x,y in pairs)}
out['fixed_depth_incomplete']=sum(x['results'][k]['stopped'] for x in rows for k in ('nn_d4','off_d4'))
out['examples']=[{'game':x['game'],'turn':x['turn'],'opponent':x['opponent'],'actual':x['actual'],'choices':{k:{f:v[f] for f in ('action','depth','value')} for k,v in x['results'].items()}} for x in rows if 'own_material_loss' in x['kinds'] and x['results']['nn49']['action']!=x['results']['nn500']['action']]
(D/'summary.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
# Export selected positions with the full actual observation/action prefix.
R=D.parent/'nn-41710886/arena';games={}
for line in (R/'recon78.txt').read_text().splitlines():
 if line.startswith('G '):_,gid,w=line.split();g={'walls':list(map(int,w)),'states':[],'actions':[]};games[gid]=g
 elif line.startswith('S '):s,a=json.loads('['+line[2:]+']');g['states'].append(s);g['actions'].append(a)
corpus=[]
for r in rows:
 g=games[r['game']];t=r['turn'];corpus.append(dict(game=r['game'],turn=t,kinds=r['kinds'],walls=g['walls'],states=g['states'][:t+1],actions=g['actions'][:t+1]))
(D/'corpus.json').write_text(json.dumps(corpus))
