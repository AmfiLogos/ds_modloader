#!/usr/bin/env python3
"""Teams -> expedition, end to end, on a leased hidden pair (MoonlightBrigade base save).

  teams_expedition_pair.py [--dylib PATH] [--keep]

Friend (Y) takes a free unit, marks it, readies. Host (X) takes two free units, marks one, readies.
dsnet EXPEDITION_START runs on both at the same step; the host sends ONE ChangeMapRequested (Base ->
Dungeon, IndexList = the marked units) through trn_submit_map_change. Checks on BOTH peers: the start is
seen, both land in the dungeon, the dungeon's player units are exactly the two marked units, owners are
kept, fingerprints equal for 1000+ ticks after landing. Also: the open Teams screen makes the game's own
over-UI test (UnityUtil.IsPointerOverUIByRaycast) true, so world clicks become UI clicks."""
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
        rig.start(X, fps=10)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        closed = tm(X)
        a, b = tm(X, action='open'), tm(Y, action='open')
        check('in the base, screen open on both', a['open'] and b['open'] and a['in_dungeon'] == 0 and b['in_dungeon'] == 0)
        check('open screen = over UI for the game (world clicks blocked)', a['over_ui'] == 1 and b['over_ui'] == 1,
              f"closed {closed['over_ui']} -> open X {a['over_ui']} Y {b['over_ui']} (mouse {a['mouse']})")
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('3 free units', len(free) >= 3, f'{len(free)}')
        u1, u2, u3 = free[0], free[1], free[2]
        log('dungeon units before', player_units(X, 1))

        tm(Y, action='click', guid=u1, button=0)
        settle(X, Y, lambda a, b: col_of(b, u1) == 1, 'Y claim')
        tm(Y, action='click', guid=u1, button=1)
        settle(X, Y, lambda a, b: T.widget(b, u1)['selected'] == 1, 'Y mark')
        for u in (u2, u3):                                                   # the host marks first: Y's proposal takes allies' marks
            tm(X, action='click', guid=u, button=0)
            settle(X, Y, lambda a, b, u=u: col_of(a, u) == 0 and col_of(b, u) == 0, 'host takes')
        tm(X, action='click', guid=u2, button=1)
        settle(X, Y, lambda a, b: T.widget(a, u2)['selected'] == 1 and T.widget(b, u2)['selected'] == 1, 'X mark')
        T.propose(Y)
        a, b = settle(X, Y, lambda a, b: b['ready'] == 2 and a['prop']['active'] and a['prop']['members'] == 2, 'Y proposes')
        check("Y's proposal holds the host's marked unit too (2 members)", a['prop']['members'] == 2, json.dumps(a['prop']))
        t_ready = st(X)['tick']
        tm(X, action='approve')
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'expedition start', timeout=60)
        check('EXPEDITION_START seen on both, party of 2', a['starts'] == 1 and b['starts'] == 1 and a['start_units'] == 2 and b['start_units'] == 2,
              f"X {a['starts']}/{a['start_units']} Y {b['starts']}/{b['start_units']}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        t_land = max(st(X)['tick'], st(Y)['tick'])
        dx, dy = player_units(X, 1), player_units(Y, 1)
        check('both land in the dungeon with exactly the marked units', dx == sorted([u1, u2]) and dy == sorted([u1, u2]),
              f'X {dx} Y {dy} want {sorted([u1, u2])}')
        a, b = tm(X), tm(Y)
        check('owners kept (u1 Friend, u2 + u3 host) on both',
              all(col_of(s, u1) == 1 and col_of(s, u2) == 0 and col_of(s, u3) == 0 for s in (a, b)))
        check('screen locked in the dungeon', a['in_dungeon'] == 1 and b['in_dungeon'] == 1)
        wait('1000 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 1100, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t_land)
        check('fingerprints equal every tick (1000+ after landing)', n >= 1000 and first is None, f'common ticks {n}, first diff {first}')
        n2, first2 = fp_equal_from(X, Y, t_ready)
        log('fingerprints from the ready click:', n2, 'first diff', first2)
        for i in (X, Y):
            with open(os.path.join(rig.idir(i), 'rig.log'), errors='replace') as f:
                for line in f:
                    if 'teams:' in line or 'expedition start' in line or 'map change' in line.lower():
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
