"""Fine-tune the reference NNUE; select on map-disjoint validation data only."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch
from torch import nn

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from model import Net, REC, NF, NUF, QA, QB, make_feats, export

def load_net(path):
    model = Net(64)
    raw = path.read_bytes()
    values = np.frombuffer(raw[:-16], dtype='<i2').copy()
    assert len(raw) == (NF * 64 + 64 + 4 * 128) * 2 + 16
    w = values[:NF * 64].reshape(NF, 64) / QA
    b = values[NF * 64:NF * 64 + 64] / QA
    o = values[NF * 64 + 64:].reshape(4, 128) / QB
    ob = np.frombuffer(raw[-16:], dtype='<i4').copy() / (QA * QB)
    with torch.no_grad():
        model.emb.weight[:NUF].copy_(torch.tensor(w[:NUF]))
        model.emb.weight[NUF].zero_()
        model.wall.weight.copy_(torch.tensor(w[NUF:].T))
        model.wall.bias.copy_(torch.tensor(b))
        model.out.weight.copy_(torch.tensor(o))
        model.out.bias.copy_(torch.tensor(ob))
    return model

def canonical(w):
    board = np.unpackbits(w, bitorder='little')[:91].reshape(7, 13)
    return min(x.tobytes() for x in (board, board[::-1], board[:, ::-1], board[::-1, ::-1]))

def dataset(split, excluded):
    data = np.concatenate([np.fromfile(p, dtype=REC) for p in sorted((ROOT / 'data').glob(f'{split}_*.bin'))])
    keys = [canonical(w) for w in data['walls']]
    keep = np.array([k not in excluded for k in keys])
    data = data[keep]
    maps = set(k for k, good in zip(keys, keep) if good)
    assert len(data) > 0
    u = torch.from_numpy(data['unit'].astype(np.int16))
    wl = torch.from_numpy(data['walls'].copy())
    t = torch.from_numpy(data['turn'].copy())
    feats = make_feats('cpu', u, wl, t)
    score = np.clip(data['score'].astype(np.float32), -4000, 4000)
    margin = data['margin'].astype(np.float32)
    y = .4 / (1 + np.exp(-.7 * margin)) + .6 / (1 + np.exp(-score / 800))
    result = (data['result'].astype(np.float32) + 1) / 2
    return (feats, torch.tensor(y), torch.tensor(result)), maps, int((~keep).sum())

def evaluate(model, ds):
    feats, y, result = ds
    sums = np.zeros(2)
    with torch.no_grad():
        for start in range(0, len(y), 2048):
            idx = torch.arange(start, min(start + 2048, len(y)))
            z = model(*feats(idx, None))
            sums += np.array([nn.functional.binary_cross_entropy_with_logits(z, target[idx]).item()
                              for target in (y, result)]) * len(idx)
    return dict(target_bce=float(sums[0] / len(y)), result_bce=float(sums[1] / len(y)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--epochs', type=int, default=20)
    args = ap.parse_args()
    torch.set_num_threads(4)
    torch.manual_seed(1411)
    np.random.seed(1411)
    train, trmaps, _ = dataset('train', set())
    val, vmaps, dropped_val = dataset('val', trmaps)
    test, tmaps, dropped_test = dataset('test', trmaps | vmaps)
    model = load_net(ROOT / 'teacher.bin')
    report = dict(seed=1411, epochs=args.epochs, lr=.0002, batch=1024,
                  positions=dict(train=len(train[1]), val=len(val[1]), test=len(test[1])),
                  maps=dict(train=len(trmaps), val=len(vmaps), test=len(tmaps)),
                  excluded_overlap=dict(val=dropped_val, test=dropped_test),
                  teacher_val=evaluate(model, val), teacher_test=evaluate(model, test), history=[])
    print(json.dumps(report), flush=True)
    optimizer = torch.optim.AdamW(model.parameters(), lr=.0002, weight_decay=0)
    batches = (len(train[1]) + 1023) // 1024
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, args.epochs * batches, eta_min=1e-6)
    best = float('inf')
    feats, y, _ = train
    for epoch in range(args.epochs):
        order = torch.randperm(len(y))
        total = 0.
        for start in range(0, len(y), 1024):
            idx = order[start:start + 1024]
            # No vertical flip: Bresenham tie-breaking is not exactly symmetric.
            loss = nn.functional.binary_cross_entropy_with_logits(model(*feats(idx, None)), y[idx])
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()
            scheduler.step()
            with torch.no_grad():
                model.emb.weight.clamp_(-127 / QA, 127 / QA)
                model.emb.weight[NUF].zero_()
                model.wall.weight.clamp_(-127 / QA, 127 / QA)
                model.out.weight.clamp_(-1.98, 1.98)
            total += loss.item() * len(idx)
        export(model, ROOT / 'last.bin')
        quantized = load_net(ROOT / 'last.bin')
        metrics = dict(epoch=epoch + 1, train_bce=total / len(y), **evaluate(quantized, val))
        report['history'].append(metrics)
        print(json.dumps(metrics), flush=True)
        if metrics['target_bce'] < best:
            best = metrics['target_bce']
            report['best_epoch'] = epoch + 1
            (ROOT / 'candidate.bin').write_bytes((ROOT / 'last.bin').read_bytes())
            torch.save(model.state_dict(), ROOT / 'candidate.pt')
    selected = load_net(ROOT / 'candidate.bin')
    report['candidate_val'] = evaluate(selected, val)
    report['candidate_test'] = evaluate(selected, test)
    report['sha256'] = hashlib.sha256((ROOT / 'candidate.bin').read_bytes()).hexdigest()
    report['torch'] = torch.__version__
    (ROOT / 'training.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'history'}), flush=True)

if __name__ == '__main__':
    main()
