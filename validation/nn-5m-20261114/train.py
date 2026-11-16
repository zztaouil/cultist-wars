"""Controlled capacity experiment; immutable input cache, validation-only selection."""
import argparse, copy, hashlib, json, random, time
from pathlib import Path
import numpy as np
import torch
from torch import nn

ROOT = Path(__file__).resolve().parent
CACHE = ROOT / 'cache'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--reuse-audit', action='store_true')
    p.add_argument('--init', type=Path)
    p.add_argument('--lr', type=float, default=.0003)
    p.add_argument('--threads', type=int, default=6)
    p.add_argument('--batch-size', type=int, default=512)
    p.add_argument('--hidden', type=int, nargs='+', default=[128, 64])
    p.add_argument('--seed', type=int, default=17)
    p.add_argument('--epochs', type=int, default=100)
    p.add_argument('--shuffle-block', type=int, default=8192)
    p.add_argument('--patience', type=int, default=12)
    p.add_argument('--minutes', type=float, default=120)
    p.add_argument('--device', default='cpu')
    p.add_argument('--out', required=True)
    p.add_argument('--cache', type=Path, default=CACHE)
    args = p.parse_args()
    cache = args.cache
    out = ROOT / args.out
    out.mkdir(parents=True, exist_ok=False)
    random.seed(args.seed); np.random.seed(args.seed); torch.manual_seed(args.seed)
    torch.set_num_threads(args.threads)
    start = time.monotonic()
    print(json.dumps({'stage':'loading','cache':str(cache)}),flush=True)
    m = json.loads((cache / 'meta.json').read_text())
    x = np.memmap(cache / 'features.f32', dtype='<f4', mode='r').reshape(-1, 128)
    offsets = np.asarray(m['offsets']); lengths = np.diff(offsets)
    y = np.asarray(m['labels']); splits = np.asarray(m['splits'])
    assert len(x) == offsets[-1]
    assert np.all((y >= 0) & (y < lengths))
    if args.reuse_audit:
        audit = json.loads((cache/'audit.json').read_text())
        assert hashlib.sha256((cache/'meta.json').read_bytes()).hexdigest() == audit['meta_sha256']
        digest = hashlib.sha256()
        with (cache/'records.jsonl').open('rb') as records:
            for block in iter(lambda: records.read(8*1024*1024), b''): digest.update(block)
        assert digest.hexdigest() == audit['records_sha256']
        assert not audit['map_leakage'] and audit['heldouts_original_only']
        assert audit['positions'] == len(y)
        map_groups = audit['map_groups']
    else:
        for chunk in range(0, len(x), 100000):
            assert np.isfinite(x[chunk:chunk+100000]).all()
        groups = {}; record_count = 0
        for rec, split in zip(map(json.loads, (cache/'records.jsonl').open()), splits):
            groups.setdefault(rec['map'], set()).add(int(split)); record_count += 1
        assert record_count == len(y)
        assert all(len(s) == 1 for s in groups.values())
        map_groups = len(groups)
    ids = [np.flatnonzero(splits == s) for s in range(3)]
    architecture = [128, *args.hidden, 1]
    layers = []
    for i, (a, b) in enumerate(zip(architecture, architecture[1:])):
        layers.append(nn.Linear(a, b))
        if i < len(architecture) - 2: layers.append(nn.ReLU())
    model = nn.Sequential(*layers).to(args.device)
    if args.init:
        checkpoint = torch.load(args.init, map_location=args.device, weights_only=False)
        assert checkpoint['architecture'] == architecture
        assert checkpoint['feature_version'] == m['feature_version']
        model.load_state_dict(checkpoint['state_dict'])
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=.0001)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, patience=3, factor=.5, min_lr=1e-6)
    def batch(ii, block_x=None, base=0):
        lens = lengths[ii]; width = int(lens.max())
        rows = np.repeat(np.arange(len(ii)), lens)
        cols = np.concatenate([np.arange(n) for n in lens])
        indices = np.repeat(offsets[ii], lens) + cols
        features = torch.from_numpy(x[indices] if block_x is None else block_x[indices-base]).to(args.device)
        z = torch.full((len(ii), width), -1e9, device=args.device)
        z[torch.as_tensor(rows, device=args.device), torch.as_tensor(cols, device=args.device)] = model(features).flatten()
        return z, torch.as_tensor(y[ii], device=args.device)
    @torch.no_grad()
    def evaluate(ii):
        loss = top1 = top3 = 0
        for jj in np.array_split(ii, max(1, int(np.ceil(len(ii)/args.batch_size)))):
            z, target = batch(jj)
            loss += nn.functional.cross_entropy(z, target, reduction='sum').item()
            top1 += (z.argmax(1) == target).sum().item()
            top3 += (z.topk(min(3, z.shape[1]), dim=1).indices == target[:, None]).any(1).sum().item()
        return dict(loss=loss/len(ii), top1=top1/len(ii), top3=top3/len(ii), positions=len(ii))
    print(json.dumps({'stage':'training','split_positions':[len(i) for i in ids]}),flush=True)
    initial = evaluate(ids[1]); print(json.dumps({'initial_validation': initial}), flush=True)
    (out/'initial.json').write_text(json.dumps(initial, indent=2))
    best = initial['loss']; best_state = copy.deepcopy(model.state_dict()); best_epoch = 0; stale = 0; history = []
    torch.save(dict(state_dict=best_state, architecture=architecture, feature_version=m['feature_version'], baseline_sha256=m['baseline_sha256']), out/'best.pt')
    rng = np.random.default_rng(args.seed)
    for epoch in range(1, args.epochs+1):
        t = time.monotonic(); model.train(); total = 0.
        # Shuffle contiguous blocks and rows within each block to bound mmap working set.
        blocks = [ids[0][i:i+args.shuffle_block] for i in range(0,len(ids[0]),args.shuffle_block)]
        seen = 0
        for block_number, bi in enumerate(rng.permutation(len(blocks))):
            block = blocks[bi]
            base = offsets[block[0]]
            block_x = np.array(x[base:offsets[block[-1]+1]])
            order = rng.permutation(block)
            for jj in np.array_split(order, int(np.ceil(len(order)/args.batch_size))):
                optimizer.zero_grad(set_to_none=True)
                z, target = batch(jj, block_x, base); loss = nn.functional.cross_entropy(z, target)
                assert torch.isfinite(loss)
                loss.backward(); optimizer.step(); total += loss.item()*len(jj)
                seen += len(jj)
            if (block_number+1)%100 == 0:
                print(json.dumps(dict(epoch=epoch, processed=seen, seconds=round(time.monotonic()-t,2))),flush=True)
        model.eval(); val = evaluate(ids[1])
        row = dict(epoch=epoch, train_loss=total/seen, validation=val, seconds=round(time.monotonic()-t, 2))
        scheduler.step(val['loss']); row['lr'] = optimizer.param_groups[0]['lr']
        history.append(row); print(json.dumps(row), flush=True)
        if val['loss'] < best-.0001:
            best = val['loss']; best_state = copy.deepcopy(model.state_dict()); best_epoch = epoch; stale = 0
            torch.save(dict(state_dict=best_state, architecture=architecture, feature_version=m['feature_version'], baseline_sha256=m['baseline_sha256']), out/'best.pt')
        else: stale += 1
        (out/'history.json').write_text(json.dumps(history, indent=2))
        if stale >= args.patience or time.monotonic()-start >= args.minutes*60: break
    model.load_state_dict(best_state); model.eval()
    weights = {k: v.detach().cpu().numpy() for k,v in model.state_dict().items()}
    np.savez(out/'policy_weights.npz', **weights)
    (out/'policy_weights.json').write_text(json.dumps({k:v.tolist() for k,v in weights.items()}))
    report = dict(architecture=architecture, parameters=sum(v.numel() for v in model.parameters()),
        seed=args.seed, best_epoch=best_epoch, epochs=len(history), seconds=round(time.monotonic()-start, 2),
        validation=evaluate(ids[1]), test=evaluate(ids[2]), config={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
        torch_version=torch.__version__, numpy_version=np.__version__,
        meta_sha256=hashlib.sha256((cache/'meta.json').read_bytes()).hexdigest(),
        baseline_sha256=m['baseline_sha256'], feature_version=m['feature_version'],
        split_positions=[len(i) for i in ids], map_groups=map_groups,
        dataset=json.loads((cache/'report.json').read_text()),
        notes=['Original-only test split; map grouping reused from prior research, not globally untouched.',
               'Checkpoint selected only by validation cross entropy.',
               'Imitation metrics do not establish search strength.'])
    (out/'report.json').write_text(json.dumps(report, indent=2)); print(json.dumps(report), flush=True)

if __name__ == '__main__': main()
