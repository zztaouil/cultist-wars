"""Export a stopped training stage's best checkpoint without inspecting test data."""
import argparse,json
from pathlib import Path
import numpy as np
import torch
p=argparse.ArgumentParser();p.add_argument('stage');args=p.parse_args();R=Path(__file__).resolve().parent;d=R/args.stage
c=torch.load(d/'best.pt',map_location='cpu',weights_only=False);hist=json.loads((d/'history.json').read_text());initial=json.loads((d/'initial.json').read_text())
best=initial;epoch=0
for row in hist:
 if row['validation']['loss']<best['loss']-.0001:best=row['validation'];epoch=row['epoch']
w={k:v.numpy() for k,v in c['state_dict'].items()};np.savez(d/'policy_weights.npz',**w);(d/'policy_weights.json').write_text(json.dumps({k:v.tolist() for k,v in w.items()}))
r=dict(architecture=c['architecture'],parameters=sum(v.size for v in w.values()),best_epoch=epoch,epochs=len(hist),validation=best,feature_version=c['feature_version'],baseline_sha256=c['baseline_sha256'],note='Stage deliberately stopped for lower-LR refinement; best complete-epoch checkpoint retained. No test evaluation at this stage.')
(d/'report.json').write_text(json.dumps(r,indent=2));print(json.dumps(r),flush=True)
