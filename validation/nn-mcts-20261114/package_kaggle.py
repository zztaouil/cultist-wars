"""Build private-upload bundle and notebook; performs no upload or remote run."""
import hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def cell(kind,text):
    c=dict(cell_type=kind,metadata={},source=text.splitlines(keepends=True))
    if kind=='code':c.update(execution_count=None,outputs=[])
    return c
cells=[cell('markdown','''# Cultist Wars: shared policy/value pilot
Enable GPU T4 ×2. Attach `nn-mcts-kaggle.zip` as a private Kaggle dataset. This notebook trains on verified expert replay outcomes, not MCTS self-play. Historical map splits are not a fresh strength holdout. Two-GPU path implemented but not GPU-tested locally. Small model may train faster on CPU due to GPU coordination overhead.
'''),cell('code','''from pathlib import Path
import zipfile, subprocess, sys, torch
bundle = next(Path('/kaggle/input').rglob('nn-mcts-kaggle.zip'))
work = Path('/kaggle/working/nn-mcts')
work.mkdir(exist_ok=False)
with zipfile.ZipFile(bundle) as z:
    z.extractall(work)
print(torch.__version__, 'CUDA devices:', torch.cuda.device_count())
'''),cell('code','''import hashlib, json
manifest = json.loads((work/'bundle-manifest.json').read_text())
for name, expected in manifest.items():
    assert hashlib.sha256((work/name).read_bytes()).hexdigest() == expected, name
print('Bundle checksums verified')
'''),cell('code','''# Batch size is per GPU. Fresh output directory required for each run.
args = ['train.py', '--cache', 'cache', '--out', 'trained', '--epochs', '30', '--batch-size', '512']
if torch.cuda.device_count() >= 2:
    command = [sys.executable, '-m', 'torch.distributed.run', '--standalone', '--nproc_per_node=2', *args]
else:
    command = [sys.executable, *args, '--device', 'cuda' if torch.cuda.is_available() else 'cpu']
subprocess.run(command, cwd=work, check=True)
'''),cell('code','''subprocess.run([sys.executable, 'export.py', '--checkpoint', 'trained/best.pt'], cwd=work, check=True)
print((work/'trained/report.json').read_text())
subprocess.run(['cc', '-O3', '-std=c11', 'test_mcts.c', '-lm', '-o', 'test_mcts'], cwd=work, check=True)
subprocess.run([str(work/'test_mcts')], cwd=work, check=True)
# Download output bundle; local parity and equal-time matches still required.
with zipfile.ZipFile('/kaggle/working/nn-mcts-results.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ['trained/best.pt', 'trained/report.json', 'trained/history.json', 'candidate.c', 'packed.npy', 'export.json']:
        z.write(work/name, name)
''')]
notebook=dict(cells=cells,metadata=dict(kernelspec=dict(display_name='Python 3',language='python',name='python3'),language_info=dict(name='python')),nbformat=4,nbformat_minor=5)
(ROOT/'kaggle.ipynb').write_text(json.dumps(notebook,indent=2))
files=['train.py','export.py','baseline.c','pv.h','mcts.h','test_mcts.c']+[str(p.relative_to(ROOT)) for p in sorted((ROOT/'cache').iterdir()) if p.is_file()]
manifest={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files}
with zipfile.ZipFile(ROOT/'nn-mcts-kaggle.zip','w',zipfile.ZIP_DEFLATED,compresslevel=3) as z:
    for name in files:z.write(ROOT/name,name)
    z.writestr('bundle-manifest.json',json.dumps(manifest,indent=2))
print(json.dumps(dict(bundle_bytes=(ROOT/'nn-mcts-kaggle.zip').stat().st_size,files=len(files))))
