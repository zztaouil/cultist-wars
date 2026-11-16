#!/usr/bin/env python3
"""Train the NNUE-style evaluation for the Cultist Wars bot.

Data: 48-byte records written by `CW_GEN` (see runGen in bot.cpp).
Inputs, per perspective (the perspective player's own leader starts on the left: the
board is mirrored left-right for player 1):
  unit features  (type * 4 + hp bucket) * 91 + cell      1820
     type: 0 my leader, 1 enemy leader, 2 my cultist, 3 enemy cultist, 4 neutral
     hp bucket: 1-2, 3-4, 5-6, 7-10 (a shot does at most 6)
  obstacle features  1820 + cell                           91
Net: shared feature transformer 1911 -> H for both perspectives, SCReLU, 2H -> one logit
per output bucket (bucket = plies left: >70, >30, >10, rest).
Output file: int16 W[1911][H], int16 B[H], int16 O[buckets][2][H], int32 OB[buckets]
(QA=255, QB=64), the layout bot.cpp loads with CW_NET / embeds for submission.

  train.py -o tools/nets/n0.bin 'data/g0_*.bin' [--hidden 32] [--epochs 20] [--lam 0.6]
"""
import argparse, glob, os, re, time
import numpy as np
import torch
import torch.nn as nn

REC = np.dtype([("walls", "u1", 12), ("unit", "<u2", 14), ("turn", "u1"), ("result", "i1"), ("margin", "i1"),
                ("flags", "u1"), ("score", "<i2"), ("pad", "u1", 2)])
QA, QB = 255, 64
W_, H_, NC = 13, 7, 91
NUF = 5 * 4 * NC          # unit features; index NUF is the zero padding row (dead unit)
NF = NUF + NC
HPB = [0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3]


class Net(nn.Module):
    def __init__(self, h, buckets=4):
        super().__init__()
        self.h, self.nb = h, buckets
        self.emb = nn.Embedding(NUF + 1, h, padding_idx=NUF)
        self.wall = nn.Linear(NC, h)            # its bias is the feature transformer bias
        self.out = nn.Linear(2 * h, buckets)
        nn.init.uniform_(self.emb.weight, -0.1, 0.1)
        nn.init.uniform_(self.wall.weight, -0.05, 0.05)
        nn.init.constant_(self.wall.bias, 0.1)
        with torch.no_grad():
            self.emb.weight[NUF].zero_()

    def acc(self, idx, walls):
        return torch.clamp(self.emb(idx).sum(1) + self.wall(walls), 0, 1) ** 2

    def forward(self, ia, wa, ib, wb, bk):
        z = self.out(torch.cat([self.acc(ia, wa), self.acc(ib, wb)], dim=1))
        return z.gather(1, bk[:, None]).squeeze(1)


def make_feats(dev, U, WL, T, nbk=4):
    """Returns feats(idx, yflip) -> (unit idx, walls) for both perspectives + output bucket, built on `dev`."""
    hpb = torch.tensor(HPB, device=dev)
    bits = torch.tensor([[(v >> i) & 1 for i in range(8)] for v in range(256)], dtype=torch.float32, device=dev)
    isLeader = (torch.arange(14, device=dev) < 2)[None, :]
    uid = torch.arange(14, device=dev)[None, :]

    def feats(idx, yflip):
        u = U[idx].long()
        pos, hp, own = u & 127, (u >> 7) & 15, (u >> 11) & 3
        x, y = pos % W_, pos // W_
        turn = T[idx].long()
        walls = bits[WL[idx].long()].reshape(-1, 96)[:, :NC].reshape(-1, H_, W_)
        if yflip is not None:
            y = torch.where(yflip[:, None], H_ - 1 - y, y)
            walls = torch.where(yflip[:, None, None], walls.flip(1), walls)
        out = []
        for other in (0, 1):
            p = ((turn & 1) ^ other)[:, None]                 # perspective player
            xx = torch.where(p == 1, W_ - 1 - x, x)
            typ = torch.where(isLeader, (uid != p).long(), torch.where(own == p, 2, torch.where(own == 2, 4, 3)))
            f = (typ * 4 + hpb[hp]) * NC + y * W_ + xx
            f = torch.where(hp > 0, f, NUF)
            wl = torch.where((p == 1)[:, :, None], walls.flip(2), walls).reshape(-1, NC)
            out += [f, wl]
        left = 150 - turn
        bk = (left <= 70).long() + (left <= 30).long() + (left <= 10).long()
        if nbk != 4:
            bk = bk * 0
        return (*out, bk)
    return feats


def export(model, path):
    h = model.h
    w = np.concatenate([model.emb.weight.detach().cpu().numpy()[:NUF], model.wall.weight.detach().cpu().numpy().T])
    w = np.round(w * QA).astype(np.int16)                                                   # [NF][H]
    b = np.round(model.wall.bias.detach().cpu().numpy() * QA).astype(np.int16)              # [H]
    o = np.round(model.out.weight.detach().cpu().numpy().reshape(model.nb, 2, h) * QB).astype(np.int16)
    ob = np.round(model.out.bias.detach().cpu().numpy() * QA * QB).astype(np.int32)
    with open(path, "wb") as f:
        f.write(w.tobytes()); f.write(b.tobytes()); f.write(o.tobytes()); f.write(ob.tobytes())
    return int(np.abs(w).max()), int(np.abs(o).max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--hidden", type=int, default=32)
    ap.add_argument("--buckets", type=int, default=4)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--lam", type=float, default=0.6, help="weight of game result vs search score")
    ap.add_argument("--k", type=float, default=600.0, help="score scale for sigmoid(score/k)")
    ap.add_argument("--no-yflip", action="store_true", help="no up-down mirror augmentation")
    ap.add_argument("--margin-w", type=float, default=0.0, help="share of the result target replaced by sigmoid(margin-c * final unit margin)")
    ap.add_argument("--margin-c", type=float, default=1.0)
    ap.add_argument("--wd", type=float, default=0.0)
    ap.add_argument("--wclip", type=float, default=127 / 255, help="clip first-layer weights (int8 at QA=255); 0 = off")
    ap.add_argument("--init", help="float checkpoint (.pt) to continue from")
    ap.add_argument("--extra", action="append", default=[], help="extra records (e.g. rebuilt arena games), training only, target = game result")
    ap.add_argument("--extra-rep", type=int, default=1, help="times each extra record is repeated per epoch")
    ap.add_argument("--rep", action="append", default=[], help="REGEX:N - files whose name matches REGEX are loaded N times (weight recent generations)")
    ap.add_argument("--lam-file", action="append", default=[], help="REGEX:LAM - result weight for files whose name matches REGEX")
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else
                    ("mps" if torch.backends.mps.is_available() else "cpu"))
    a = ap.parse_args()
    t0 = time.time()

    paths = sorted(set(p for f in a.files for p in glob.glob(f, recursive=True)))
    reps = [(re.compile(r.rsplit(":", 1)[0]), int(r.rsplit(":", 1)[1])) for r in a.rep]
    times = [max([n for rx, n in reps if rx.search(os.path.basename(p))] or [1]) for p in paths]
    lfs = [(re.compile(r.rsplit(":", 1)[0]), float(r.rsplit(":", 1)[1])) for r in a.lam_file]
    chunks = [np.fromfile(p, dtype=REC) for p in paths]
    flam = [next((l for rx, l in lfs if rx.search(os.path.basename(p))), a.lam) for p in paths]
    data = np.concatenate([c for c, t in zip(chunks, times) for _ in range(t)])
    lam = np.concatenate([np.full(len(c), l, np.float32) for c, l, t in zip(chunks, flam, times) for _ in range(t)])
    del chunks
    if reps:
        print(f"repeated: {sum(t > 1 for t in times)} files", flush=True)
    if lfs:
        print(f"own result weight: {sum(l != a.lam for l in flam)} files", flush=True)
    perm = np.random.default_rng(1).permutation(len(data))
    data, lam = data[perm], lam[perm]
    n = len(data)
    nval = min(200000, n // 20)
    res = (data["result"].astype(np.float32) + 1) / 2
    print(f"{n} positions from {len(paths)} files; stm result win {np.mean(res == 1):.3f} draw {np.mean(res == .5):.3f}", flush=True)
    hard = res
    if a.margin_w > 0:      # a win by three units is a surer win than a win by one
        soft = 1 / (1 + np.exp(-a.margin_c * data["margin"].astype(np.float32)))
        res = ((1 - a.margin_w) * res + a.margin_w * soft).astype(np.float32)
    sc = np.clip(data["score"].astype(np.float32), -4000, 4000)
    target = (lam * res + (1 - lam) / (1 + np.exp(-sc / a.k))).astype(np.float32)
    del lam
    epaths = sorted(set(p for f in a.extra for p in glob.glob(f, recursive=True)))
    if epaths:              # appended after the shuffle: never in the validation slice
        ex = np.concatenate([np.fromfile(p, dtype=REC) for p in epaths])
        eres = (ex["result"].astype(np.float32) + 1) / 2
        if a.margin_w > 0:
            esoft = 1 / (1 + np.exp(-a.margin_c * ex["margin"].astype(np.float32)))
            eres = ((1 - a.margin_w) * eres + a.margin_w * esoft).astype(np.float32)
        ex, eres = np.tile(ex, a.extra_rep), np.tile(eres, a.extra_rep)
        print(f"extra: {len(ex) // a.extra_rep} positions x{a.extra_rep} from {len(epaths)} files", flush=True)
        data = np.concatenate([data, ex])
        hard = np.concatenate([hard, (ex["result"].astype(np.float32) + 1) / 2])
        target = np.concatenate([target, eres.astype(np.float32)])
        n = len(data)

    dev = torch.device(a.device)
    U = torch.from_numpy(data["unit"].astype(np.int16)).to(dev)            # (n, 14)
    WL = torch.from_numpy(np.ascontiguousarray(data["walls"])).to(dev)     # (n, 12) bytes
    T = torch.from_numpy(data["turn"].astype(np.uint8)).to(dev)
    Y = torch.from_numpy(target).to(dev)
    R = torch.from_numpy(hard).to(dev)
    del data
    feats = make_feats(dev, U, WL, T, a.buckets)

    model = Net(a.hidden, a.buckets).to(dev)
    if a.init:
        model.load_state_dict(torch.load(a.init, map_location=dev))
    opt = torch.optim.AdamW(model.parameters(), lr=a.lr, weight_decay=a.wd)
    nb = (n - nval) // a.batch
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=a.epochs * nb, eta_min=a.lr / 400)
    lossf = nn.BCEWithLogitsLoss()

    def evaluate():
        model.eval()
        tot = tot2 = 0.0
        with torch.no_grad():
            for i in range(0, nval, 65536):
                idx = torch.arange(i, min(i + 65536, nval), device=dev)
                z = model(*feats(idx, None))
                tot += float(lossf(z, R[idx])) * len(idx)
                tot2 += float(lossf(z, Y[idx])) * len(idx)
        model.train()
        return tot / nval, tot2 / nval

    print(f"loaded in {time.time() - t0:.0f}s, training on {a.device}, {nb} batches/epoch", flush=True)
    t0 = time.time()
    for ep in range(a.epochs):
        perm = (torch.randperm(n - nval) + nval).to(dev)
        run = torch.zeros((), device=dev)
        for i in range(nb):
            idx = perm[i * a.batch:(i + 1) * a.batch]
            yf = None if a.no_yflip else torch.rand(len(idx), device=dev) < 0.5
            loss = lossf(model(*feats(idx, yf)), Y[idx])
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            sched.step()
            with torch.no_grad():
                model.out.weight.clamp_(-1.98, 1.98)
                model.emb.weight[NUF].zero_()
                if a.wclip:
                    model.emb.weight.clamp_(-a.wclip, a.wclip)
                    model.wall.weight.clamp_(-a.wclip, a.wclip)
                run += loss.detach()
        vr, vt = evaluate()
        print(f"epoch {ep + 1}/{a.epochs} train {float(run) / nb:.4f} val(result) {vr:.4f} val(target) {vt:.4f} ({time.time() - t0:.0f}s)", flush=True)
        torch.save(model.state_dict(), a.out + ".pt")
        mw, mo = export(model, a.out)
    print(f"saved {a.out} (max |W| {mw}, max |O| {mo})")


if __name__ == "__main__":
    main()
