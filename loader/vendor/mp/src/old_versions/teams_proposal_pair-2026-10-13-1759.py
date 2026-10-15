#!/usr/bin/env python3
"""Expedition proposal (owner 2026-10-13, replaces the region vote) on a leased hidden pair.

  teams_proposal_pair.py [--dylib PATH] [--keep]

X hosts MoonlightBrigade (det on), Y joins. Units: X takes + marks f0 f3, Y takes + marks f1 f2 (4 members).
  party cap   X also marks f4 (5 members) and presses Go -> refused (no proposal, not ready); X unmarks f4
  decline     Y proposes -> the window opens on both (X: Approve / Decline, Y: Cancel), 4 / 4 members;
              X declines -> closed on both, "The owner declined the expedition", nobody ready
  cancel      Y proposes, Y cancels -> "Friend cancelled the expedition"
  timeout     X proposes, Y does nothing -> after 60 s the host ends it: "No answer from Friend", nobody ready
  approve     Y proposes region 0, X approves -> EXPEDITION_START on both with f0 f1 f2 f3, both land, the
              map change carries the proposal's region; fingerprints equal.
Frames: research/ux_shots/v2_proposal_*.png."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, widget, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.widget, T.settle, T.fp_equal_from
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')


def shot(x, name):
    p = f'{OUT}/v2_proposal_{name}.png'
    log('frame', p, 'rc', tm(x, action='shot', path=p).get('rc'))


def player_units(x, m):
    r = c(x, cmd='list_units', map=m)
    return sorted(u['guid'] for u in r.get('units', []) if u['faction'] == 1 and not u['dead'])


def take_mark(x, guids):
    for g in guids:
        tm(x, action='click', guid=g, button=0); time.sleep(1.5)
    for g in guids:
        tm(x, action='click', guid=g, button=1); time.sleep(1.5)


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
    pair = rig.lease('teams', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        a = tm(X)
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('5 free units', len(free) >= 5, f'{len(free)}')
        f = free[:5]
        t0 = st(X)['tick']
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)            # unit clicks go through the Teams screen
        take_mark(X, [f[0], f[3]]); take_mark(Y, [f[1], f[2]])
        a, b = settle(X, Y, lambda a, b: a['prop']['members'] == 4 and b['prop']['members'] == 4, 'marks', timeout=60)
        check('marks: X f0 f3, Y f1 f2 (4 members, cap 4, limit 3)', a['prop']['members'] == 4 and a['prop']['cap'] == 4 and a['prop']['mark_limit'] == 3,
              json.dumps(a['prop']))
        # party cap
        tm(X, action='click', guid=f[4], button=0); time.sleep(1.5); tm(X, action='click', guid=f[4], button=1)
        a, b = settle(X, Y, lambda a, b: a['prop']['members'] == 5, 'X marks a 5th', timeout=60)
        tm(X, action='go'); time.sleep(4)
        a, b = tm(X), tm(Y)
        check('party cap: 5 members -> Go refused (no proposal, nobody ready)', not a['prop']['active'] and not b['prop']['active'] and a['ready'] == 0,
              f"members {a['prop']['members']} active {a['prop']['active']} ready {a['ready']}")
        tm(X, action='click', guid=f[4], button=1)
        settle(X, Y, lambda a, b: a['prop']['members'] == 4 and b['prop']['members'] == 4, 'X unmarks', timeout=60)
        # decline
        tm(Y, action='go')
        a, b = settle(X, Y, lambda a, b: a['prop']['shown'] and b['prop']['shown'], 'proposal shown', timeout=60)
        check('proposal from Friend shown on both: X Approve / Decline, Y Cancel, 4 / 4 members',
              a['prop']['active'] and b['prop']['active'] and a['prop']['proposer'] == 1 and b['prop']['proposer'] == 1
              and a['prop']['ok'] == 'Approve' and a['prop']['no'] == 'Decline' and b['prop']['ok'] == '' and b['prop']['no'] == 'Cancel'
              and a['prop']['members'] == 4 and 'you' in a['prop']['wait_text'] and 'The owner' in b['prop']['wait_text'] and b['ready'] == 2,
              json.dumps(a['prop']) + ' / ' + json.dumps(b['prop']))
        check('proposal window is modal (over UI) on both', a['over_ui'] == 1 and b['over_ui'] == 1)
        time.sleep(1); shot(X, 'open_host'); shot(Y, 'open_guest')
        tm(X, action='decline')
        a, b = settle(X, Y, lambda a, b: not a['prop']['active'] and not b['prop']['active'] and a['ready'] == 0 and b['ready'] == 0, 'declined', timeout=60)
        check('decline: closed on both, "The owner declined the expedition", nobody ready',
              not a['prop']['shown'] and not b['prop']['shown'] and a['prop']['event'] == 'The owner declined the expedition'
              and b['prop']['event'] == 'The owner declined the expedition' and a['ready'] == 0 and b['ready'] == 0 and a['starts'] == 0,
              f"{a['prop']['event']!r} / {b['prop']['event']!r} ready {a['ready']}/{b['ready']}")
        shot(Y, 'declined_guest')
        # cancel
        tm(Y, action='go')
        settle(X, Y, lambda a, b: a['prop']['active'] and b['prop']['active'], 'proposal 2', timeout=60)
        tm(Y, action='cancel')
        a, b = settle(X, Y, lambda a, b: not a['prop']['active'] and not b['prop']['active'] and b['ready'] == 0, 'cancelled', timeout=60)
        check('cancel by the proposer: "Friend cancelled the expedition" on both, nobody ready',
              a['prop']['event'] == 'Friend cancelled the expedition' and b['prop']['event'] == 'Friend cancelled the expedition' and b['ready'] == 0,
              f"{a['prop']['event']!r} / {b['prop']['event']!r}")
        # timeout
        tm(X, action='go')
        a, b = settle(X, Y, lambda a, b: a['prop']['shown'] and b['prop']['shown'], 'proposal 3', timeout=60)
        check('host proposes: Y sees Approve / Decline, X sees Cancel', b['prop']['ok'] == 'Approve' and a['prop']['no'] == 'Cancel' and a['prop']['proposer'] == 0)
        time.sleep(2); shot(X, 'waiting_host')
        tp = time.time()
        a, b = settle(X, Y, lambda a, b: not a['prop']['active'] and not b['prop']['active'], 'timeout', timeout=120)
        dt = time.time() - tp
        check('timeout: ended after about 60 s, "No answer from Friend" on both, nobody ready',
              50 <= dt + 2 <= 90 and a['prop']['event'] == 'No answer from Friend' and b['prop']['event'] == 'No answer from Friend'
              and a['ready'] == 0 and b['ready'] == 0 and a['starts'] == 0,
              f"{dt:.0f} s {a['prop']['event']!r} / {b['prop']['event']!r} ready {a['ready']}/{b['ready']}")
        shot(X, 'timeout_host')
        # approve
        tm(Y, action='go', region=0)
        settle(X, Y, lambda a, b: a['prop']['shown'] and b['prop']['shown'], 'proposal 4', timeout=60)
        tm(X, action='approve')
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('approve: EXPEDITION_START on both, 4 units, proposal closed, region of the proposal',
              a['starts'] == 1 and b['starts'] == 1 and a['start_units'] == 4 and b['start_units'] == 4 and not a['prop']['active'] and not b['prop']['active']
              and a['prop']['start_region'] == 0 and b['prop']['start_region'] == 0,
              f"starts {a['starts']}/{b['starts']} units {a['start_units']}/{b['start_units']} region {a['prop']['start_region']}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        want = sorted(f[:4])
        dx, dy = player_units(X, 1), player_units(Y, 1)
        check('both land with exactly the proposed parties (f0 f1 f2 f3)', dx == want and dy == want, f'X {dx} Y {dy} want {want}')
        t_land = max(st(X)['tick'], st(Y)['tick'])
        wait('300 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 300, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n >= 300 and first is None, f'common ticks {n}, first diff {first}')
        for i in (X, Y):
            with open(os.path.join(rig.idir(i), 'rig.log'), errors='replace') as fh:
                for line in fh:
                    if 'teams: proposal' in line or 'expedition' in line and 'teams:' in line:
                        last = line
            log(i, '| last:', last.rstrip()[:200])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
