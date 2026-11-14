"""Reconstruct saved logs with unmodified Java model, check C transitions and summaries."""
import sys,json,re,tempfile,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lmr'))
from run import build,setup,Array,Move
D=Path(__file__).resolve().parent;ROOT=D.parents[1]
paths=list((ROOT/'validation/lmr').glob('boss-baseline-200[01].json'))
paths+=list((ROOT/'validation/consistency').glob('budget35-*.json'))
paths+=list((ROOT/'replays/top10-2026-11-13').glob('*.json'))
items=[]; summaries=[]
with tempfile.TemporaryDirectory() as td:
 t=Path(td);subprocess.run(['javac','-d',td,*map(str,(ROOT/'source/src/main/java/model').glob('*.java')),str(D/'Replay.java')],check=True)
 lib=build(D/'baseline.c',t,'baseline')
 for path in paths:
  data=json.loads(path.read_text());log=data.get('log','')
  if not log and 'frames' in data:
   log='options: '+data['game_options']+'\n'+'\n'.join('frame '+str(f['frame'])+' agent '+str(f.get('agent',0))+'\n  summary: '+f.get('summary','')+'\n  stdout: '+f.get('stdout','') for f in data['frames'] if f.get('stdout'))
  seed=re.search(r'options:.*seed=(-?\d+)',log)
  if not seed:continue
  frames=[]
  for m in re.finditer(r'frame (\d+) agent (\d+)\n(.*?)(?=\nframe |\nresult|\Z)',log,re.S):
   out=re.search(r'  stdout: (.+)',m[3]);summary=re.search(r'  summary: (.*?)(?=\n  \w+:|\Z)',m[3],re.S)
   if out and 'timeout' not in m[3]:frames.append((int(m[1]),int(m[2]),out[1].split(';')[0].strip(),summary[1] if summary else ''))
   elif 'timeout' in m[3]:break
  if not frames:continue
  result=subprocess.run(['java','-cp',td,'model.Replay',seed[1]],input='\n'.join(x[2] for x in frames)+'\n',text=True,capture_output=True)
  if result.returncode:raise RuntimeError(str(path)+result.stderr)
  rows=[json.loads(x) for x in result.stdout.splitlines()];w=rows.pop(0)['walls'];setup(lib,w)
  for idx,(before,after) in enumerate(zip(rows,rows[1:])):
   state=Array(*before['state']);lib.step(state,Move(*before['action'],0))
   assert list(state)==after['state'],(path,idx,'C/Java transition mismatch')
   summary=frames[idx][3]
   hit=re.search(r'Unit (\d+) hit unit (\d+). New hp: (\d+)',summary)
   move=re.search(r'Unit (\d+) moved to row (\d+) col (\d+)',summary)
   conv=re.search(r'Unit (\d+) converted unit (\d+)',summary)
   if hit:assert after['state'][4*int(hit[2])+1]==int(hit[3]),(path,idx,'HP')
   if move:assert after['state'][4*int(move[1])]==int(move[3])*13+int(move[2]),(path,idx,'move')
   if conv:assert after['state'][4*int(conv[2])+2]==idx%2,(path,idx,'convert')
   items.append(dict(name=f'{path.stem}-turn{idx}',game=path.stem,seed=int(seed[1]),turn=idx,walls=w,state=before['state'],action=before['action'],summary=summary,source=str(path.relative_to(ROOT))))
  summaries.append(dict(game=path.stem,seed=int(seed[1]),transitions=len(frames),final_counts=[sum(rows[-1]['state'][4*i+1]>0 and rows[-1]['state'][4*i+2]==s for i in range(14)) for s in (0,1)]))
(D/'replay-positions.json').write_text(json.dumps(items));(D/'reconstruction.json').write_text(json.dumps(summaries,indent=2));print(json.dumps(summaries,indent=2))
