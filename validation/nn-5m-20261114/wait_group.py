import subprocess,sys,time
from pathlib import Path
R=Path(__file__).resolve().parent;deadline=time.monotonic()+1800
while not (R/'search-cost.json').exists():
 if time.monotonic()>deadline:raise TimeoutError('Initial evaluation did not finish')
 time.sleep(5)
subprocess.run([sys.executable,str(R/'check_group.py')],cwd=R.parents[1],check=True)
