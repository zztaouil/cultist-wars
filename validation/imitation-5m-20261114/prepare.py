"""Verify real games; expand teacher inputs with reflections and approved ID relabeling."""
import argparse
import array
import ctypes as C
import gzip
import hashlib
import json
import random
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BASE = ROOT.parent / 'nn-41710886'
TARGET = 5_000_000


def reflect_pos(p, t):
    return (12-p % 13 if t & 1 else p % 13) + 13*(6-p//13 if t & 2 else p//13)


def reflect(w, s, a, t):
    ww = [0]*91
    for pos, value in enumerate(w):
        ww[reflect_pos(pos, t)] = value
    ss = s.copy()
    for j in range(14):
        ss[j*4] = reflect_pos(s[j*4], t) if s[j*4+1] > 0 else 0
    aa = a.copy()
    if aa[0] == 1:
        aa[2] = reflect_pos(aa[2], t)
    return ww, ss, aa


def id_permutations():
    result = [list(range(14))]
    rng = random.Random(17005)
    while len(result) < 32:
        tail = list(range(2, 14))
        rng.shuffle(tail)
        candidate = [0, 1] + tail
        if candidate not in result:
            result.append(candidate)
    return result


def relabel(s, a, permutation):
    ss = s.copy()
    for old, new in enumerate(permutation):
        ss[new*4:new*4+4] = s[old*4:old*4+4]
    ss[59] = sum(1 << permutation[j] for j in range(14) if s[59] & (1 << j))
    aa = a.copy()
    aa[1] = permutation[a[1]]
    if a[0] in (2, 3):
        aa[2] = permutation[a[2]]
    return ss, aa


def observable_digest(w, s):
    observable = s[:58]
    for j in range(14):
        if observable[j*4+1] == 0:
            observable[j*4:j*4+4] = [0]*4
    return hashlib.blake2b(bytes(w)+array.array('i', observable).tobytes(), digest_size=16).digest()


def tactical_digest(w, s):
    # Deliberately ignore IDs: these counts expose repeated tactical boards.
    units = sorted(tuple(s[j*4:j*4+4]) if s[j*4+1] else (0, 0, 0, 0) for j in range(14))
    values = [v for unit in units for v in unit] + s[56:58]
    return hashlib.blake2b(bytes(w)+array.array('i', values).tobytes(), digest_size=16).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--corpus', type=Path, default=ROOT/'games.jsonl.gz')
    parser.add_argument('--cache', type=Path, default=ROOT/'cache')
    parser.add_argument('--max-games', type=int)
    parser.add_argument('--target', type=int, default=TARGET)
    args = parser.parse_args()
    assert args.target > 0
    start = time.monotonic()
    build = ROOT/'build'
    build.mkdir(exist_ok=True)
    subprocess.run(['javac', '-d', str(build/'java'), *map(str, (BASE/'java/model').glob('*.java'))], check=True)
    (build/'features.c').write_text('#include "'+str(BASE/'features.c')+'"\nint shot_victim(const int*p,const int*a){State s=unpack(p);return victim(&s,(Action){a[0],a[1],a[2],0});}\n')
    subprocess.run(['cc', '-O3', '-std=c11', '-shared', '-fPIC', str(build/'features.c'), '-o', str(build/'features.so')], check=True)
    lib = C.CDLL(str(build/'features.so'))
    A, M, W = C.c_int*62, C.c_int*3, C.c_int*91
    lib.setup.argtypes = [C.POINTER(C.c_int)]
    lib.extract.argtypes = [C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_float), C.POINTER(C.c_int), C.POINTER(C.c_int)]
    lib.verify_step.argtypes = [C.POINTER(C.c_int)]*3
    lib.shot_victim.argtypes = [C.POINTER(C.c_int)]*2
    games = {str(g['id']): g for g in map(json.loads, gzip.open(args.corpus, 'rt'))}
    experts = {r['nickname'] for r in json.loads((ROOT/'leaderboard.json').read_text())['rows']}
    gids = sorted(games)
    random.Random(17).shuffle(gids)
    if args.max_games:
        gids = gids[:args.max_games]
    with (build/'input.txt').open('w') as f:
        for gid in gids:
            g = games[gid]
            if 'TIMEOUT' in g['actions'] or not g['actions']:
                continue
            f.write(f"G {gid} {g['ref'].strip().split('=')[1]} {len(g['actions'])}\n"+'\n'.join(g['actions'])+'\n')
    with (build/'input.txt').open() as inp, (build/'recon.txt').open('w') as out:
        subprocess.run(['java', '-cp', str(build/'java'), 'model.Batch'], stdin=inp, stdout=out, check=True)
    cache = args.cache
    cache.mkdir(exist_ok=False)
    xf, rf = (cache/'features.f32').open('wb'), (cache/'records.jsonl').open('w')
    permutations = id_permutations()
    offsets, labels, splits = [0], [], []
    seen, valid_games, map_info = set(), set(), {}
    counts, variants, perm_counts = [0]*3, [0]*4, [0]*len(permutations)
    players = {}
    stats = dict(corpus_games=len(games), timeout_or_empty_games_excluded=sum('TIMEOUT' in g['actions'] or not g['actions'] for g in games.values()), games_seen=0, games_valid=0, games_rejected=0, transitions_verified=0, original_positions=0, duplicates=0, reflection_ray_rejected=0, unmatched=0, forced=0)
    buf, order, label = (C.c_float*(253*128))(), (C.c_int*253)(), C.c_int()

    def consume(gid, w, states, acts, bad, pid):
        g = games[gid]
        if pid == 0:
            stats['games_seen'] += 1
            final = [sum(states[-1][j*4+1] > 0 and states[-1][j*4+2] == side for j in range(14)) for side in (0, 1)] if states else []
            if bad or final != g['scores'] or len(states) != len(g['actions'])+1:
                stats['games_rejected'] += 1
                return
            lib.setup(W(*w))
            for i in range(len(states)-1):
                if not lib.verify_step(A(*states[i]), M(*acts[i]), A(*states[i+1])):
                    stats['games_rejected'] += 1
                    return
            valid_games.add(gid)
            stats['games_valid'] += 1
            stats['transitions_verified'] += len(states)-1
            key = hashlib.sha256(bytes(min(reflect(w, states[0], acts[0], t)[0] for t in range(4)))).hexdigest()
            bucket = int(key[:8], 16) % 100
            map_info[gid] = (key, 0 if bucket < 80 else 1 if bucket < 90 else 2)
        elif gid not in valid_games:
            return
        key, split = map_info[gid]
        if pid and split:
            return  # Heldouts always retain original IDs and original geometry.
        lib.setup(W(*w))
        names = {a['index']: a['pseudo'] for a in g['agents']}
        candidates = [i for i, s in enumerate(states[:-1]) if names[s[56]] in experts]
        victims = {i: lib.shot_victim(A(*states[i]), M(*acts[i])) for i in candidates}
        permutation = permutations[pid]
        for t in (range(4) if split == 0 else [0]):
            ww = reflect(w, states[0], acts[0], t)[0]
            lib.setup(W(*ww))
            for i in candidates:
                if split == 0 and counts[0] >= args.target:
                    break
                _, original, aa = reflect(w, states[i], acts[i], t)
                ss, aa = relabel(original, aa, permutation)
                expected = permutation[victims[i]] if victims[i] >= 0 else -1
                if acts[i][0] == 2 and lib.shot_victim(A(*ss), M(*aa)) != expected:
                    stats['reflection_ray_rejected'] += 1
                    continue
                n = lib.extract(A(*ss), M(*aa), buf, order, C.byref(label))
                if label.value < 0:
                    stats['unmatched'] += 1
                    continue
                if n < 2:
                    stats['forced'] += 1
                    continue
                digest = observable_digest(ww, ss)
                if digest in seen:
                    stats['duplicates'] += 1
                    continue
                seen.add(digest)
                xf.write(C.string_at(buf, n*128*4))
                offsets.append(offsets[-1]+n)
                labels.append(label.value)
                splits.append(split)
                counts[split] += 1
                variants[t] += 1
                perm_counts[pid] += 1
                stats['original_positions'] += int(t == 0 and pid == 0)
                players[names[states[i][56]]] = players.get(names[states[i][56]], 0)+1
                rf.write(json.dumps(dict(game=gid, turn=i, player=names[states[i][56]], map=key, reflection=t, id_permutation=pid, synthetic=bool(t or pid), state_sha=digest.hex(), tactical_sha=tactical_digest(ww, original)))+'\n')

    # Complete every original/reflected input first, preserving the old dataset.
    # Only then add ID variants, without claiming additional played boards.
    for pid in range(len(permutations)):
        if pid and counts[0] >= args.target:
            break
        pass_games = 0
        for line in (build/'recon.txt').open():
            if line.startswith('G '):
                _, gid, ws = line.split()
                w, states, acts, bad = list(map(int, ws)), [], [], False
            elif line.startswith('S '):
                s, a = json.loads('['+line[2:]+']')
                states.append(s)
                acts.append(a)
            elif line.startswith('X '):
                bad = True
            elif line.startswith('E'):
                consume(gid, w, states, acts, bad, pid)
                pass_games += 1
                if pass_games % 200 == 0:
                    print(json.dumps(dict(permutation_pass=pid, pass_games=pass_games, split_positions=counts, seconds=round(time.monotonic()-start))), flush=True)
                if pid and counts[0] >= args.target:
                    break
    xf.close()
    rf.close()
    meta = dict(offsets=offsets, labels=labels, splits=splits, feature_version=1, augmentation_version=2, unit_id_permutations=permutations, teacher_cohort_sha256=hashlib.sha256((ROOT/'leaderboard.json').read_bytes()).hexdigest(), corpus_sha256=hashlib.sha256(args.corpus.read_bytes()).hexdigest(), preparation_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), baseline_sha256=hashlib.sha256((BASE/'baseline.c').read_bytes()).hexdigest())
    (cache/'meta.json').write_text(json.dumps(meta))
    stats.update(positions=len(labels), split_positions=counts, reflection_counts=variants, permutation_counts=perm_counts, id_relabelled_positions=sum(perm_counts[1:]), geometry_only_positions=perm_counts[0], synthetic_positions=len(labels)-stats['original_positions'], player_positions=players, feature_bytes=offsets[-1]*128*4, seconds=round(time.monotonic()-start), target_train_positions=args.target, target_reached=counts[0] == args.target)
    (cache/'report.json').write_text(json.dumps(stats, indent=2))
    print(json.dumps(stats), flush=True)
    assert all(n > 100 for n in counts)
    assert stats['target_reached'], 'Insufficient unique augmented teacher inputs for target'


if __name__ == '__main__':
    main()
