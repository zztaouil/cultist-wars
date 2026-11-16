from experiment import *
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--names',nargs='+',default=['baseline','neutral','ray','metadata','packed','undo']);parser.add_argument('--reps',type=int,default=5);parser.add_argument('--out',default='screen');parser.add_argument('--positions',default='positions.json');parser.add_argument('--depth',type=int,default=5);args=parser.parse_args()
ps=json.loads((D/args.positions).read_text());libs={n:build(n) for n in args.names};rows=[]
for rep in range(args.reps):
 for pi,p in enumerate(ps):
  order=args.names if (rep+pi)%2==0 else args.names[::-1]
  results={}
  for name in order:
   lib=libs[name];r=run(lib,p,args.depth);results[name]=r
   t=T();lib.micro(A(*p['state']),2000,t)
   rows.append(dict(rep=rep,position=pi,name=name,**r,micro_ns=t[0],transitions=t[1],state_bytes=t[2],action_bytes=t[3],entry_bytes=t[4]))
  base=results[args.names[0]]
  for name,r in results.items():
   assert {k:v for k,v in r.items() if k!='ns'}=={k:v for k,v in base.items() if k!='ns'},(rep,pi,name,base,r)
 (D/(args.out+'.json')).write_text(json.dumps(rows));print('rep',rep,'done',flush=True)
summary={}
for name in args.names:
 rr=[r for r in rows if r['name']==name];times=[sum(r['ns'] for r in rr if r['rep']==rep)/1e9 for rep in range(args.reps)];micro=[sum(r['transitions'] for r in rr if r['rep']==rep)/(sum(r['micro_ns'] for r in rr if r['rep']==rep)/1e9) for rep in range(args.reps)]
 summary[name]=dict(seconds=times,median_s=statistics.median(times),min_s=min(times),max_s=max(times),transitions_sec=micro,median_transitions_sec=statistics.median(micro),visits_per_rep=sum(r['visits'] for r in rr if r['rep']==0),action_bytes=rr[0]['action_bytes'],entry_bytes=rr[0]['entry_bytes'])
(D/(args.out+'-summary.json')).write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
