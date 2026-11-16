"""Wait for this experiment's two finite training runs, then evaluate locally."""
import subprocess,sys,time
from pathlib import Path
R=Path(__file__).resolve().parent;deadline=time.monotonic()+3600
while not all((R/n/'report.json').exists() for n in ['fine-small','fine-wide']):
 if time.monotonic()>deadline: raise TimeoutError('Training reports did not arrive within one hour')
 time.sleep(10)
print('Both training runs finished; starting export and evaluation.',flush=True)
subprocess.run([sys.executable,str(R/'run_evaluation.py')],cwd=R.parents[1],check=True)
print('Evaluation pipeline complete.',flush=True)
