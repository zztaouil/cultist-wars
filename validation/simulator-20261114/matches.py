"""Paired, seeded local matches against a saved baseline using the C simulator.

Usage: python3 tests/benchmark.py --baseline /path/to/old-bot.c --pairs 8
This is a regression sample, not an arena rating or independent referee test.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--candidate',required=True,type=Path)
parser.add_argument('--baseline', required=True, type=Path)
parser.add_argument('--pairs', type=int, default=8)
parser.add_argument('--seed-start', type=int, default=0)
parser.add_argument('--budget-ms', type=int, default=10)
args = parser.parse_args()
Array = C.c_int * 62
Move = C.c_int * 4


def load(source, directory, name, current):
    extra_read = 's.rng=(uint32_t)p[58]; s.neutral_mask=p[59]; s.rng_known=p[60];' if current else ''
    extra_write = 'p[58]=(int)s.rng; p[59]=s.neutral_mask; p[60]=s.rng_known;' if current else ''
    tracker = 'static Tracker tracker; static int turns;' if current else ''
    reset_tracker = 'memset(&tracker,0,sizeof tracker); turns=0;' if current else ''
    observe = 'track_observation(&tracker,&s,turns++==0);' if current else ''
    advance = 'track_action(&tracker,a);' if current else ''
    code = f'''#define BOT_TEST
#include "{source.resolve()}"
{tracker}
static State unpack(const int *p) {{
    State s={{0}};
    for(int i=0;i<UNITS;i++) s.u[i]=(Unit){{p[i*4],p[i*4+1],p[i*4+2],p[i*4+3]}};
    s.side=p[56]; s.remaining=p[57]; {extra_read} return s;
}}
static void pack(State s,int *p) {{
    for(int i=0;i<UNITS;i++) {{p[i*4]=s.u[i].pos;p[i*4+1]=s.u[i].hp;p[i*4+2]=s.u[i].owner;p[i*4+3]=s.u[i].leader;}}
    p[56]=s.side;p[57]=s.remaining; {extra_write}
}}
void setup(const int *w) {{ memcpy(wall,w,sizeof wall);initialize();memset(tt,0,sizeof tt);{reset_tracker} }}
void play(const int *p,int *out,int *stats) {{
    State s=unpack(p); {observe}
    int depth,value; Action a=choose(&s,&depth,&value); {advance}
    out[0]=a.kind;out[1]=a.unit;out[2]=a.target;out[3]=a.order;
    stats[0]=depth;stats[1]=(int)nodes;
}}
void step(int *p,const int *a) {{State s=unpack(p);pack(apply(&s,(Action){{a[0],a[1],a[2],a[3]}}),p);}}
int ended(const int *p) {{State s=unpack(p);int value;return terminal(&s,&value);}}
'''
    path = directory / f'{name}.c'
    path.write_text(code)
    lib = directory / f'{name}.so'
    subprocess.run(['cc', '-O3', '-std=c11', '-shared', '-fPIC',
                    f'-DBUDGET_MS={args.budget_ms}', str(path), '-o', str(lib)], check=True)
    dll = C.CDLL(str(lib))
    dll.setup.argtypes = [C.POINTER(C.c_int)]
    dll.play.argtypes = [C.POINTER(C.c_int)] * 3
    dll.step.argtypes = [C.POINTER(C.c_int)] * 2
    dll.ended.argtypes = [C.POINTER(C.c_int)]
    return dll


def fixture(seed):
    rng = random.Random(seed)
    state = Array()
    state[0:8] = [39, 10, 0, 1, 51, 10, 1, 1]
    positions = rng.sample([y * 13 + x for x in range(1, 6) for y in range(7)], 6)
    for i, pos in enumerate(positions + [p // 13 * 13 + 12 - p % 13 for p in positions], 2):
        state[i * 4:i * 4 + 4] = [pos, 10, 2, 0]
    state[56:62] = [0, 150, 42, ((1 << 14) - 1) & ~3, 1, 0]
    occupied = {state[i * 4] for i in range(14)}
    walls = (C.c_int * 91)()
    # Connected, symmetric maps. Reject obstacle pairs that disconnect floor.
    for p in rng.sample(list(range(91)), 91):
        q = p // 13 * 13 + 12 - p % 13
        if p in occupied or q in occupied or rng.random() > .10:
            continue
        walls[p] = walls[q] = 1
        seen, todo = {39}, [39]
        while todo:
            v = todo.pop()
            for d in [-13, 1, 13, -1]:
                n = v + d
                if 0 <= n < 91 and abs(n % 13 - v % 13) + abs(n // 13 - v // 13) == 1 and not walls[n] and n not in seen:
                    seen.add(n)
                    todo.append(n)
        if len(seen) != 91 - sum(walls):
            walls[p] = walls[q] = 0
    return state, walls


with tempfile.TemporaryDirectory(prefix='cultist-benchmark-') as tmp:
    directory = Path(tmp)
    current = load(args.candidate, directory, 'current', True)
    baseline = load(args.baseline, directory, 'baseline', 'Tracker' in args.baseline.read_text())
    results, depths, node_counts = [], [[], []], [[], []]
    for seed in range(args.seed_start, args.seed_start + args.pairs):
        for seat in [0, 1]:
            state, walls = fixture(seed)
            current.setup(walls)
            baseline.setup(walls)
            while not current.ended(state):
                upgraded = state[56] == seat
                lib = current if upgraded else baseline
                action, stats = Move(), (C.c_int * 2)()
                lib.play(state, action, stats)
                depths[int(upgraded)].append(stats[0])
                node_counts[int(upgraded)].append(stats[1])
                current.step(state, action)
            counts = [sum(state[i*4+1] > 0 and state[i*4+2] == side for i in range(14)) for side in [0, 1]]
            result = {'seed': seed, 'seat': seat, 'new': counts[seat], 'baseline': counts[1-seat]}
            results.append(result)
            print(json.dumps(result), flush=True)
    print(json.dumps({'budget_ms': args.budget_ms, 'games': len(results),
        'wins': sum(r['new'] > r['baseline'] for r in results),
        'draws': sum(r['new'] == r['baseline'] for r in results),
        'losses': sum(r['new'] < r['baseline'] for r in results),
        'average_depth_baseline_new': [sum(x)/len(x) for x in depths],
        'average_nodes_baseline_new': [sum(x)/len(x) for x in node_counts]}), flush=True)
