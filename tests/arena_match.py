"""Equal-time seat-paired matches on real arena maps.

Usage (one worker per core; resumable, append-only):
  python3 tests/arena_match.py --a bot.c --b bot.c:LEADERB=0 --tag lead0 \
      --maps validation/arena-verify/results/maps.json --budget-ms 25 \
      --maps-used 1200 --part 0 --parts 4 --out validation/<exp>/
  python3 tests/arena_match.py --summarize --tag lead0 --out validation/<exp>/

A = reference, B = candidate. Each map is played with B in seat 0 and seat 1.
Scores are candidate-side; the paired statistic averages the two seats of one
map, so the reported interval is not inflated by the large first-seat edge.
Specs are 'source.c[:MACRO=VAL[:MACRO=VAL...]]'; defaults of bot_r.c reproduce
production behavior.
"""
import argparse, ctypes as C, json, math, os, random, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'validation/arena-verify/scripts'))
import harness as h


def build(spec, tag, budget):
    src, *flags = spec.split(':')
    d = Path('build'); d.mkdir(exist_ok=True)
    c = d / (tag + '.c')
    c.write_text(h.CODE.replace('SOURCE', str((ROOT / src).resolve())))
    so = d / (tag + '.so')
    subprocess.run(['cc', '-O3', '-march=native', '-std=c11', '-shared', '-fPIC',
                    f'-DBUDGET_MS={budget}', *[f'-D{f}' for f in flags if f],
                    str(c), '-o', str(so)], check=True)
    return C.CDLL(str(so.resolve()))


def counts(s):
    return [sum(s[4 * i + 1] > 0 and s[4 * i + 2] == p for i in range(14)) for p in (0, 1)]


def summarize(path, tag):
    G = {}
    for f in sorted(Path(path).glob(f'match_{tag}_*.jsonl')):
        for line in open(f):
            r = json.loads(line)
            G.setdefault(r['map'], {})[r['seatB']] = (r['B'] > r['A']) - (r['B'] < r['A'])
    g = [v for m in G.values() for v in m.values()]
    if not g:
        print(f'{tag}: no games'); return
    w, d, l = g.count(1), g.count(0), g.count(-1)
    pr = [(m[0] + m[1]) / 2 for m in G.values() if len(m) == 2]
    pct = lambda L: (sum(x == 1 for x in L) + .5 * sum(x == 0 for x in L)) / len(L) * 100
    s0 = [m[0] for m in G.values() if 0 in m]
    s1 = [m[1] for m in G.values() if 1 in m]
    out = dict(tag=tag, games=len(g), W=w, D=d, L=l, score=round(100 * (w + d / 2) / len(g), 2),
               pairs=len(pr), seat0=round(pct(s0), 2), seat1=round(pct(s1), 2))
    if len(pr) > 1:
        mean = sum(pr) / len(pr)
        se = math.sqrt(sum((x - mean) ** 2 for x in pr) / (len(pr) - 1) / len(pr))
        out['paired_lo'] = round(50 + 50 * (mean - 1.96 * se), 2)
        out['paired_hi'] = round(50 + 50 * (mean + 1.96 * se), 2)
    print(json.dumps(out))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--a', default='bot.c'); p.add_argument('--b')
    p.add_argument('--tag', required=True); p.add_argument('--out', required=True)
    p.add_argument('--maps', default='validation/arena-verify/results/maps.json')
    p.add_argument('--budget-ms', type=int, default=25)
    p.add_argument('--maps-used', type=int, default=400)
    p.add_argument('--shuffle-seed', type=int, default=2024)
    p.add_argument('--part', type=int, default=0); p.add_argument('--parts', type=int, default=1)
    p.add_argument('--summarize', action='store_true')
    a = p.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    if a.summarize:
        summarize(out, a.tag); return
    L = [build(a.a, f'{a.tag}A{a.part}', a.budget_ms), build(a.b, f'{a.tag}B{a.part}', a.budget_ms)]
    maps = json.load(open(ROOT / a.maps))
    random.Random(a.shuffle_seed).shuffle(maps)
    fn = out / f'match_{a.tag}_{a.part}.jsonl'
    done = set()
    if fn.exists():
        for line in open(fn):
            r = json.loads(line); done.add((r['map'], r['seatB']))
    fh = open(fn, 'a')
    for gid, walls, s0 in maps[:a.maps_used][a.part::a.parts]:
        for seat in (0, 1):
            if (gid, seat) in done: continue
            for lib in L: h.setup(lib, walls)
            s = list(s0); n = 0
            while True:
                c = counts(s)
                if c[0] == 0 or c[1] == 0 or s[57] <= 0: break
                k = 1 if s[56] == seat else 0
                s = h.step(L[0], s, h.run(L[k], s)['action']); n += 1
            fh.write(json.dumps(dict(map=gid, seatB=seat, B=c[seat], A=c[1 - seat], plies=n)) + '\n')
            fh.flush()


if __name__ == '__main__':
    main()
