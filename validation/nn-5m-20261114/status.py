"""Compact progress from saved complete epochs."""
import json
from pathlib import Path
R=Path(__file__).resolve().parent
for name in ['fine-small','fine-wide']:
 d=R/name
 try:
  h=json.loads((d/'history.json').read_text());best=json.loads((d/'initial.json').read_text());epoch=0
  for row in h:
   if row['validation']['loss']<best['loss']-.0001:best=row['validation'];epoch=row['epoch']
  print(json.dumps(dict(stage=name,completed_epochs=len(h),selected_epoch=epoch,validation=best,latest_loss=h[-1]['validation']['loss'],lr=h[-1]['lr'],done=(d/'report.json').exists())),flush=True)
 except (OSError,ValueError): print(name,'initializing',flush=True)
