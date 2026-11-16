"""Exercise persistent turn parsing, legal commands and per-turn latency."""
import subprocess
import time
import sys

p = subprocess.Popen([sys.argv[1] if len(sys.argv) > 1 else './bot'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.DEVNULL, text=True, bufsize=1)
p.stdin.write('0\n13 7\n' + ('.............\n' * 7))
units = [(0, 1, 10, 0, 3, 0), (1, 1, 10, 12, 3, 1)]
for i in range(2, 14):
    units.append((i, 0, 10, 2 + (i - 2) % 9, (i - 2) // 9 + 2, i % 3))
by_id = {u[0]: u for u in units}
positions = {(u[3], u[4]) for u in units}
elapsed = []
for _ in range(12):
    t = time.monotonic()
    p.stdin.write(str(len(units)) + '\n' + ''.join(' '.join(map(str, u)) + '\n' for u in units))
    p.stdin.flush()
    result = p.stdout.readline().split()
    elapsed.append((time.monotonic() - t) * 1000)
    assert result, 'bot exited without output'
    if result == ['WAIT']:
        continue
    actor = by_id[int(result[0])]
    assert actor[5] == 0
    if result[1] == 'MOVE':
        x, y = map(int, result[2:])
        assert 0 <= x < 13 and 0 <= y < 7
        assert abs(x - actor[3]) + abs(y - actor[4]) == 1
        assert (x, y) not in positions
    else:
        target = by_id[int(result[2])]
        dist = abs(target[3] - actor[3]) + abs(target[4] - actor[4])
        if result[1] == 'SHOOT':
            assert actor[1] == 0 and 0 < dist <= 6
        else:
            assert result[1] == 'CONVERT'
            assert actor[1] == 1 and target[1] == 0 and target[5] != 0 and dist == 1
p.stdin.close()
assert p.wait(timeout=2) == 0
print(f'PASS: 12 protocol turns; max local latency {max(elapsed):.1f} ms')
