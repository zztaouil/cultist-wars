import json,collections as C,sys,glob
for tag in sys.argv[1:]:
  n=C.Counter();a=C.Counter();sh=0;lm=0;N=0
  for f in glob.glob(f'ag_{tag}_?.jsonl'):
    for l in open(f):
      r=json.loads(l)
      if r['player']=='hexagonee': continue
      n[r['player']]+=1;a[r['player']]+=r['theirs']==r['ours'];N+=1;sh+=r['ours'][0]==2
  print(f"{tag:28s} "+' '.join(f"{p} {100*a[p]/n[p]:.1f}" for p in ['aCat','Noobkins','MSz'])+f"  all {100*sum(a.values())/N:.1f}% of {N}  our-shoot-share {100*sh/N:.1f}%")
