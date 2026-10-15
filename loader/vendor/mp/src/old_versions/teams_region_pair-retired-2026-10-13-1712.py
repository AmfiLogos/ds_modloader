#!/usr/bin/env python3
"""Region vote + stock "Go to dungeon" in multiplayer, on a leased hidden pair (MoonlightBrigade base save).

  teams_region_pair.py [--dylib PATH] [--keep]

Owner 2026-10-13: each player picks a region on the Teams screen (shown per column); the expedition uses
the vote, the host breaks a tie. The stock "Go to dungeon" button (ExpeditionPlanView.InputOnAccept) in
multiplayer = Ready with your marked units + the Teams screen (and picks the panel's region).
Friend picks region A, host picks region B (1-1 tie -> B), both press the stock Accept (through the hooked
function) -> both ready -> both land in region B with exactly the two marked units, fingerprints equal."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle, T.fp_equal_from
results = T.results


def player_units(x, m):
    r = c(x, cmd='list_units', map=m)
    return sorted(u['guid'] for u in r.get('units', []) if u['faction'] == 1 and not u['dead'])


def picks(s):
    return {p: r for p, r in s['region_picks']}


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
        a, b = tm(X, action='open'), tm(Y, action='open')
        opn = a['regions_open']
        check('screen open on both, regions listed', a['open'] and b['open'] and len(opn) >= 1 and opn == b['regions_open'], f'open regions {opn}')
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        u1, u2 = free[0], free[1]

        tm(Y, action='region', button=0)                                    # Friend: first open region
        a, b = settle(X, Y, lambda a, b: picks(a).get(1, -1) >= 0 and picks(b).get(1, -1) >= 0, 'Y pick')
        ry = picks(a)[1]
        tm(X, action='region', button=0)
        settle(X, Y, lambda a, b: picks(a).get(0, -1) >= 0 and picks(b).get(0, -1) >= 0, 'X pick')
        if len(opn) >= 2:
            a = tm(X)
            if picks(a)[0] == ry:
                tm(X, action='region', button=0)                            # make the host differ: a 1-1 tie
        a, b = settle(X, Y, lambda a, b: picks(a) == picks(b) and (len(opn) < 2 or picks(a)[0] != ry), 'picks differ')
        rx = picks(a)[0]
        check('picks seen on both (Friend %d, host %d)' % (ry, rx), picks(a) == picks(b) and picks(a)[1] == ry and picks(a)[0] == rx)
        check('1-1 tie -> host region, same result on both', a['region_result'] == rx and b['region_result'] == rx,
              f"X {a['region_result']} Y {b['region_result']}" + ('' if len(opn) >= 2 else ' (only one region open: no real tie)'))
        check('region line in the bottom bar: your vote + leading region, same on both', all(s['region_text'].startswith('Region vote: ') and '(leading: ' in s['region_text']
              and '2 of 2 voted' in s['region_text'] for s in (a, b)), f"X {a['region_text']!r} Y {b['region_text']!r}")

        tm(Y, action='click', guid=u1, button=0)
        settle(X, Y, lambda a, b: col_of(b, u1) == 1, 'Y claim')
        tm(Y, action='click', guid=u1, button=1)
        settle(X, Y, lambda a, b: T.widget(b, u1)['selected'] == 1, 'Y mark')
        tm(X, action='click', guid=u2, button=0)
        settle(X, Y, lambda a, b: col_of(a, u2) == 0 and col_of(b, u2) == 0, 'host takes')
        tm(X, action='click', guid=u2, button=1)
        settle(X, Y, lambda a, b: T.widget(a, u2)['selected'] == 1, 'X mark')
        tm(X, action='close'); tm(Y, action='close')

        tm(Y, action='accept', region=ry)                                   # stock Go to dungeon (hooked)
        a, b = settle(X, Y, lambda a, b: a['ready'] == 2 and b['ready'] == 2, 'Y accept -> ready')
        check('stock Go to dungeon = Ready + Teams screen (Friend)', a['ready'] == 2 and b['ready'] == 2 and b['open'] == 1,
              f"ready {a['ready']}/{b['ready']} Y screen open {b['open']}")
        t_go = st(X)['tick']
        tm(X, action='accept', region=rx)
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('everyone ready -> start on both', a['starts'] == 1 and b['starts'] == 1)
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        sx, sy = st(X), st(Y)
        t_land = max(sx['tick'], sy['tick'])
        check('dungeon region = the vote result on both', sx.get('region') == rx and sy.get('region') == rx,
              f"X {sx.get('region')} Y {sy.get('region')} want {rx}")
        dx, dy = player_units(X, 1), player_units(Y, 1)
        check('exactly the marked units in the dungeon', dx == sorted([u1, u2]) and dy == sorted([u1, u2]), f'X {dx} Y {dy}')
        wait('1000 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 1100, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t_go)
        check('fingerprints equal every tick (from Go to 1000+ after landing)', n >= 1000 and first is None, f'common ticks {n}, first diff {first}')
        for i in (X, Y):
            with open(os.path.join(rig.idir(i), 'rig.log'), errors='replace') as f:
                for line in f:
                    if ('teams:' in line and ('region' in line or 'Go to' in line or 'expedition' in line or 'hooked' in line)) or 'expedition start' in line:
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
