"""Reproduce C/C++ fixed-depth parity and timing on identical real-map positions."""
import json
from pathlib import Path
import statistics
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
import arena_match as arena
h = arena.h
out = Path(__file__).resolve().parent
libs = [arena.build('validation/cpp-port-20261114/baseline.c', 'parity_c', 48),
        arena.build('bot.cpp', 'parity_cpp', 48)]
maps = json.loads((arena.ROOT / 'validation/arena-verify/results/maps.json').read_text())[:16]
rows = []
for gid, walls, initial in maps:
    state = list(initial)
    for ply in range(9):
        if ply % 4 == 0:
            timings = [[], []]
            for repeat in range(3):
                results = [None, None]
                for k in ([0, 1] if repeat % 2 == 0 else [1, 0]):
                    h.setup(libs[k], walls)
                    results[k] = h.run(libs[k], state, depth=6)
                    timings[k].append(results[k]['ns'])
                for key in ('action', 'value', 'nodes', 'depth'):
                    assert results[0][key] == results[1][key], (gid, ply, key, results)
            rows.append(dict(map=gid, ply=ply, nodes=results[0]['nodes'],
                             c_ns=statistics.median(timings[0]), cpp_ns=statistics.median(timings[1])))
        h.setup(libs[0], walls)
        action = h.run(libs[0], state, depth=1)['action']
        c = h.step(libs[0], state, action)
        h.setup(libs[1], walls)
        assert c == h.step(libs[1], state, action)
        state = c
result = dict(positions=len(rows), repeats=3, depth=6, parity=True,
              c_total_ns=sum(r['c_ns'] for r in rows), cpp_total_ns=sum(r['cpp_ns'] for r in rows), rows=rows)
result['cpp_speedup'] = result['c_total_ns'] / result['cpp_total_ns']
(out / 'performance.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k != 'rows'}))
