"""Local in-process timing sample; no judge timeout claim."""
import json,sys,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[1]
sys.path.insert(0,str(REPO/'tests'))
import arena_match as a
lib=a.build('bot.cpp','neural-value-timing',49)
rows=[]
for gid,w,s in json.loads((ROOT.parent/'nn-5m-20261114/match-maps.json').read_text())[:20]:
    a.h.setup(lib,w)
    for turn in range(3):
        r=a.h.run(lib,s);rows.append(r);s=a.h.step(lib,s,r['action'])
report=dict(positions=len(rows),median_ms=statistics.median(r['ns']/1e6 for r in rows),max_ms=max(r['ns']/1e6 for r in rows),median_depth=statistics.median(r['depth'] for r in rows),median_nodes=statistics.median(r['nodes'] for r in rows),rows=rows)
(ROOT/'timing.json').write_text(json.dumps(report,indent=2));print({k:v for k,v in report.items() if k!='rows'})
