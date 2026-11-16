"""Audit dataset integrity and reflection transforms independently of training."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np
from prepare import reflect,reflect_pos
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--cache',type=Path,default=ROOT/'cache');args=p.parse_args();cache=args.cache
m=json.loads((cache/'meta.json').read_text());r=json.loads((cache/'report.json').read_text())
o=np.array(m['offsets']);y=np.array(m['labels']);splits=np.array(m['splits'])
assert len(o)==len(y)+1 and len(splits)==len(y) and o[0]==0
assert np.all(np.diff(o)>=2) and np.all((y>=0)&(y<np.diff(o)))
assert (cache/'features.f32').stat().st_size==int(o[-1])*128*4
seen=set();maps={};counts=[0]*3;variants=[0]*4;players={};original_sources=set()
experts={p['nickname'] for p in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
n=0
for n,line in enumerate((cache/'records.jsonl').open(),1):
 rec=json.loads(line);split=int(splits[n-1]);assert rec['player'] in experts
 assert split==0 or rec['reflection']==0
 assert rec['state_sha'] not in seen;seen.add(rec['state_sha'])
 maps.setdefault(rec['map'],set()).add(split);counts[split]+=1;variants[rec['reflection']]+=1
 players[rec['player']]=players.get(rec['player'],0)+1
 original_sources.add((rec['game'],rec['turn']))
assert n==len(y)==r['positions'];assert counts==r['split_positions'];assert variants==r['reflection_counts']
assert all(len(s)==1 for s in maps.values());assert players==r['player_positions']
for t in range(4):
 for pos in range(91):assert reflect_pos(reflect_pos(pos,t),t)==pos
report=dict(positions=n,unique_visible_states=len(seen),source_game_turns=len(original_sources),split_positions=counts,map_groups=len(maps),reflection_counts=variants,players=players,map_leakage=False,duplicate_states=False,heldouts_original_only=True,meta_sha256=hashlib.sha256((cache/'meta.json').read_bytes()).hexdigest(),records_sha256=hashlib.sha256((cache/'records.jsonl').read_bytes()).hexdigest())
(cache/'audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
