import json
from pathlib import Path
D=Path(__file__).resolve().parent
wanted={'boss-baseline-2000':[12,24,28,32],'boss-baseline-2001':[38,40,52,54],'budget35-boss-loss':[16,18,60],'budget35-lv0sys':[26,28,32]}
ps=[p for p in json.loads((D/'replay-positions.json').read_text()) if p['turn'] in wanted.get(p['game'],[])]
(D/'corpus.json').write_text(json.dumps(ps,indent=2))
