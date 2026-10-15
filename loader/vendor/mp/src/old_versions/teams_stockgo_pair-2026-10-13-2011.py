#!/usr/bin/env python3
"""Owner bug 2026-10-13 (host's party went, guest's choice ignored): the stock "Go to dungeon" path on BOTH peers.

  teams_stockgo_pair.py [--dylib PATH] [--keep]

Calls the same core the hooked ExpeditionPlanView.InputOnAccept runs, with a panel party per peer:
  host  presses Go with [f0]                          -> takes f0, Ready; the expedition must NOT start yet;
  guest presses Go with [f1, f2, f3, f4, f0]          -> takes 3 (f1 f2 f3), 1 over the max (f4), 1 foreign (f0);
then everyone is ready -> both land with f0 f1 f2 f3 (both parties), f4 stays free in the base.
Fingerprints equal. Notices are checked in the log ("Max 3 units per player")."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle, T.fp_equal_from
results = T.results


def player_units(x, m):
    r = c(x, cmd='list_units', map=m)
    return sorted(u['guid'] for u in r.get('units', []) if u['faction'] == 1 and not u['dead'])


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
        mx, my = [os.path.getsize(os.path.join(rig.idir(i), 'rig.log')) for i in (X, Y)]
        t0 = st(X)['tick']

        tm(X, action='accept', region=0, units=f[0])                                  # host presses Go first
        a, b = settle(X, Y, lambda a, b: a['ready'] == 1 and b['ready'] == 1, 'host ready')
        time.sleep(8)
        a, b = tm(X), tm(Y)
        check("host's Go only marks Ready: no start, still in the base",
              a['starts'] == 0 and b['starts'] == 0 and st(X).get('map') == 0 and st(Y).get('map') == 0 and col_of(a, f[0]) == 0,
              f"starts {a['starts']}/{b['starts']} ready {a['ready']}")
        tm(Y, action='accept', region=0, units=','.join([f[1], f[2], f[3], f[4], f[0]]))   # guest picked 5 in the panel
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('start only after the guest is ready too, party = 1 + 3', a['starts'] == 1 and b['starts'] == 1 and a['start_units'] == 4 and b['start_units'] == 4,
              f"start units {a['start_units']}/{b['start_units']}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        want = sorted(f[:4])
        dx, dy = player_units(X, 1), player_units(Y, 1)
        check("BOTH parties land (host f0 + guest f1 f2 f3), f4 not taken", dx == want and dy == want, f'X {dx} Y {dy} want {want}')
        a, b = tm(X), tm(Y)
        check('owners: f0 host, f1-f3 guest, f4 free (both peers)',
              all(col_of(s, f[0]) == 0 and all(col_of(s, g) == 1 for g in f[1:4]) and col_of(s, f[4]) == -1 for s in (a, b)))
        with open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') as fh:
            fh.seek(my); ly = fh.read()
        check('guest told: max 3 per player, other players choose their own',
              'Max 3 units per player: 1 not taken' in ly and 'of other players: they choose their own' in ly)
        t_land = max(st(X)['tick'], st(Y)['tick'])
        wait('1000 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 1100, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n >= 1000 and first is None, f'common ticks {n}, first diff {first}')
        for i, m in ((X, mx), (Y, my)):
            with open(os.path.join(rig.idir(i), 'rig.log'), errors='replace') as fh:
                fh.seek(m)
                for line in fh:
                    if 'teams:' in line and ('Go to' in line or 'Ready' in line or 'expedition' in line or 'Max' in line):
                        log(i, '|', line.rstrip()[:220])
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
