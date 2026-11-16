"""Check exact integer inference and file-loaded vs embedded weight parity."""
import json
import os
import subprocess
import numpy as np
import torch
from train import ROOT, REC, NF, NUF, make_feats

torch.set_num_threads(2)
raw = (ROOT / 'candidate.bin').read_bytes()
v = np.frombuffer(raw[:-16], dtype='<i2').astype(np.int64)
w = v[:NF * 64].reshape(NF, 64)
b = v[NF * 64:NF * 64 + 64]
out = v[NF * 64 + 64:].reshape(4, 128)
ob = np.frombuffer(raw[-16:], dtype='<i4').astype(np.int64)
emb = np.concatenate([w[:NUF], np.zeros((1, 64), dtype=np.int64)])
checked = 0
for path in sorted((ROOT / 'data').glob('test_*.bin')):
    data = np.fromfile(path, dtype=REC)[:2000]
    f = make_feats('cpu', torch.tensor(data['unit'].astype(np.int16)),
                   torch.tensor(data['walls'].copy()), torch.tensor(data['turn'].copy()))
    ia, wa, ib, wb, bucket = [x.numpy() for x in f(torch.arange(len(data)), None)]
    aa = np.clip(emb[ia].sum(1) + wa.astype(np.int64) @ w[NUF:] + b, 0, 255)
    ab = np.clip(emb[ib].sum(1) + wb.astype(np.int64) @ w[NUF:] + b, 0, 255)
    sums = (np.concatenate([aa * aa, ab * ab], axis=1) * out[bucket]).sum(1)
    expected = np.clip(np.trunc((np.trunc(sums / 255).astype(np.int64) + ob[bucket])
                               * 800 / (255 * 64)).astype(np.int64), -20000, 20000)
    env = dict(os.environ, CW_NET=str(ROOT / 'candidate.bin'), CW_EVALCHECK=str(path))
    loaded = np.fromstring(subprocess.check_output([str(ROOT / 'build/gen')], env=env, text=True), sep=' ', dtype=np.int64)
    env.pop('CW_NET')
    embedded = np.fromstring(subprocess.check_output([str(ROOT / 'build/candidate_local')], env=env, text=True), sep=' ', dtype=np.int64)
    np.testing.assert_array_equal(expected, loaded)
    np.testing.assert_array_equal(expected, embedded)
    checked += len(data)
size = len((ROOT / 'bot.cpp').read_text())
assert size < 100000
result = dict(positions=checked, max_integer_error=0, embedded_parity=True, source_characters=size)
(ROOT / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
