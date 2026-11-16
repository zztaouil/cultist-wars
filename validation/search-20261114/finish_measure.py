import time,subprocess,sys
from pathlib import Path
D=Path(__file__).resolve().parent
while sum(len(p.read_text().splitlines()) for p in D.glob('match_efficient49_*.jsonl'))<64:time.sleep(5)
subprocess.run([sys.executable,str(D/'measure.py')],check=True)
