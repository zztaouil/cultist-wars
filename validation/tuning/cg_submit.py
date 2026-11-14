"""Private boss tests and arena submission for the tuned bot.

Reads the token from ~/.config/codingame/token, or from $CG_TOKEN_FILE if set
(never prints it). Run from the repository root on the machine holding the
token:

  python3 validation/tuning/cg_submit.py --boss            # 4 fresh seeds, both orders
  python3 validation/tuning/cg_submit.py --submit          # submit bot.c, poll, archive
  python3 validation/tuning/cg_submit.py --boss --submit

Boss games are informational only: 8 games cannot separate a 2-point change.
The submission decision rests on validation/tuning/README.md.
"""
import argparse, hashlib, json, time, urllib.error, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
D = Path(__file__).resolve().parent
API = 'https://www.codingame.com/api/v1'


def token_path():
    """CG_TOKEN_FILE overrides the default, for hosts that mount the token
    elsewhere (e.g. a container with the folder bind-mounted)."""
    import os
    return Path(os.environ.get('CG_TOKEN_FILE',
                               Path.home() / '.config/codingame/token'))


def request(path, body=None):
    token = token_path().read_text().strip()
    req = urllib.request.Request(
        API + path, data=json.dumps(body).encode() if body is not None else None,
        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        print(json.dumps(json.load(e)))
        raise SystemExit(1)


def cooldown(payload):
    windows = (payload.get('limits') or {}).get('test_games', [])
    if isinstance(windows, dict):
        windows = [windows]
    wait = max([w.get('resets_in_seconds', 0) for w in windows if w.get('remaining', 1) < 2] + [0])
    if wait:
        time.sleep(wait + 1)


def parse_log(log):
    """Pull the final scores out of a finished test-game log."""
    for line in reversed((log or '').splitlines()):
        if 'scores:' in line:
            got = dict(p.split('=') for p in line.split('scores:')[1].split())
            mine, theirs = int(got.get('0', 0)), int(got.get('1', 0))
            return dict(me=mine, boss=theirs,
                        result='W' if mine > theirs else 'L' if mine < theirs else 'D')
    return dict(me=None, boss=None, result='?')


def done_already(out):
    """Only a finished game counts as done; an interrupted poll must resume."""
    if not out.exists():
        return False
    try:
        return json.loads(out.read_text()).get('status') == 'done'
    except ValueError:
        return False


def boss(seeds, candidate, baseline, budget_s=None):
    start = time.time()
    for seed in seeds:
        order = ['baseline', 'tuned'] if seed % 2 == 0 else ['tuned', 'baseline']
        for variant in order:
            out = D / f'boss-{variant}-{seed}.json'
            if done_already(out):
                continue
            if budget_s and time.time() - start > budget_s:
                print(json.dumps(dict(stopped='time budget; rerun to continue')))
                return
            src = baseline if variant == 'baseline' else candidate
            p = request('/puzzles/cultist-wars/test-games?wait=30',
                        dict(language='C', code=src.read_text(), opponents=['boss'],
                             game_options='seed=' + str(seed)))
            out.write_text(json.dumps(p, indent=2))
            while p.get('status') not in ('done', 'failed'):
                if not p.get('test_game_id'):
                    raise RuntimeError(p)
                p = request('/test-games/%s?wait=30' % p['test_game_id'])
                out.write_text(json.dumps(p, indent=2))
            print(json.dumps(dict(file=out.name, status=p['status'], seed=seed,
                                  variant=variant, **parse_log(p.get('log')))), flush=True)
            if p['status'] == 'failed':
                raise RuntimeError('boss test failed; inspect ' + out.name)
            cooldown(p)


def submit(candidate, change):
    """POST exactly once, then archive the source immediately so the id is
    never lost if polling is interrupted."""
    code = candidate.read_text()
    p = request('/puzzles/cultist-wars/submissions', dict(language='C', code=code))
    sid = p.get('submission_id')
    print(json.dumps(dict(submitted=sid, limits=p.get('limits'))), flush=True)
    (ROOT / 'submissions' / f'{sid}.c').write_text(code)
    (ROOT / 'submissions' / f'{sid}.json').write_text(json.dumps(
        dict(submission_id=sid, sha256=hashlib.sha256(code.encode()).hexdigest(),
             change=change, progress='pending'), indent=2))
    return sid


def poll(change, timeout_s=150):
    """Poll arena progress; safe to call repeatedly until it reports done."""
    start = time.time()
    while True:
        g = request('/puzzles/cultist-wars/progress')
        print(json.dumps({k: g.get(k) for k in
                          ('submission_id', 'percentage', 'score', 'rank', 'total',
                           'league', 'league_rank', 'eligible_for_promotion',
                           'in_progress')}), flush=True)
        done = not g.get('in_progress') and g.get('percentage') == 100
        if done or time.time() - start > timeout_s:
            break
        time.sleep(15)
    if done:
        sid = g.get('submission_id')
        (ROOT / 'submissions' / f'{sid}.json').write_text(json.dumps(
            dict(submission_id=sid, change=change, progress=g), indent=2))
        (D / 'cg-progress-after.json').write_text(json.dumps(g, indent=2))
        print('FINAL', json.dumps({k: g.get(k) for k in
                                   ('score', 'rank', 'total', 'league', 'league_rank',
                                    'league_total', 'eligible_for_promotion')}))
        print('BOSS', json.dumps(g.get('league_boss')))
    return done


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--boss', action='store_true')
    ap.add_argument('--submit', action='store_true')
    ap.add_argument('--poll', action='store_true', help='poll arena progress only')
    ap.add_argument('--no-wait', action='store_true', help='submit without polling')
    ap.add_argument('--change', default='leader bonus 420 -> 0')
    ap.add_argument('--seeds', type=int, nargs='+', default=[5000, 5001, 5002, 5003])
    ap.add_argument('--budget-s', type=int, default=None,
                    help='stop launching new boss games after this many seconds')
    ap.add_argument('--candidate', default='bot.c')
    ap.add_argument('--baseline', default='validation/tuning/pre-leader-bonus.c')
    a = ap.parse_args()
    cand, base = ROOT / a.candidate, ROOT / a.baseline
    print(json.dumps(dict(candidate=a.candidate,
                          sha256=hashlib.sha256(cand.read_bytes()).hexdigest()[:16])))
    if a.boss:
        boss(a.seeds, cand, base, a.budget_s)
    if a.submit:
        submit(cand, a.change)
        if not a.no_wait:
            poll(a.change)
    elif a.poll:
        poll(a.change)
    if not (a.boss or a.submit or a.poll):
        print('nothing to do: pass --boss, --submit and/or --poll')
