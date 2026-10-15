#!/usr/bin/env python3
"""Level-up popup in multiplayer, on a leased pair (CD or EF): owner rule = opens only for the unit's
owner and never pauses anyone.

  levelup_pair.py [--dylib PATH] [--keep]

X hosts and loads the ElvenExpedition save (det on); Y joins through the join point. X claims a player
unit. `ui_levelup` sends one mp order: on every peer, at the same step, the sim gets
InteractBuildingRequested(unit, BUILDING_WoodGodnessStatue) -> LevelUpHandler -> popup 21.
Checks: popup opened on X only, the time lock skipped, both copies keep ticking, no pause, fingerprints
equal every tick from the claim on."""
import json, os, shutil, sys, time
RIG = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(0, RIG)
import rig

PORT = 7791
STATUE = '9d5ff5a9-6c54-4f4e-9e9e-0181de2d6573'      # BUILDING_WoodGodnessStatue in the ElvenExpedition save (base map)
results = {}


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    for _ in range(10):
        s = c(x, cmd='status')
        if 'tick' in s:
            return s
        time.sleep(3)
    raise RuntimeError(f'{x}: no status: {s}')


def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, info)


def log_since(x, mark):
    with open(os.path.join(rig.idir(x), 'rig.log'), errors='replace') as f:
        f.seek(mark)
        return f.read()


def fp_equal_from(x, y, t_from):
    def rd(i):
        d = {}
        for line in open(os.path.join(rig.idir(i), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from:
                d[int(p[0])] = p[1]
        return d
    a, b = rd(x), rd(y)
    com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)


def inject(x, ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}
    d.update({k.replace('_', '@'): v for k, v in f.items()})
    return c(x, **d)


def order_move(x, guid, cell):          # same input path as rig/mp2_test.py
    c(x, cmd='ord_hover', n=0)
    r1 = inject(x, 'SelectPlayerUnitRequested', i32_16=0, guid_20=guid)
    r2 = inject(x, 'InteractGridRequested', v2_16=f'{cell[0]},{cell[1]}', u8_24=0)
    return r1.get('ok') and r2.get('ok')


def fight(X, Y, us):
    """mp2_test's fight phase: each side sends its units to the nearest enemy; log every popup and time
    lock (the mp2 run had a popup pause both copies after the fight)."""
    players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
    enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
    mine = {X: players[:2], Y: players[2:4]}
    for u in mine[Y]:
        log(Y, 'claim', u['name'], c(Y, cmd='mp_claim', guid=u['guid']))
    if len(mine[X]) > 1:
        log(X, 'claim', mine[X][1]['name'], c(X, cmd='mp_claim', guid=mine[X][1]['guid']))
    time.sleep(3)
    marks = {i: os.path.getsize(os.path.join(rig.idir(i), 'rig.log')) for i in (X, Y)}
    d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
    for x in (X, Y):
        for u in mine[x]:
            e = min(enemies, key=lambda e: d2(u, e))
            log('fight:', x, u['name'][:10], '->', e['cell'], order_move(x, u['guid'], e['cell']))
    t0 = st(X)['tick']; last = (t0, time.time())
    while st(X)['tick'] < t0 + 4000:
        t = st(X)['tick']
        if t != last[0]:
            last = (t, time.time())
        elif time.time() - last[1] > 30:
            log('fight: world stopped at', t, 'X', st(X).get('mp'), 'Y', st(Y).get('mp'))
            break
        time.sleep(5)
    for i in (X, Y):
        for line in log_since(i, marks[i]).splitlines():
            if 'ui:' in line or 'AddPauseReason' in line or 'GameSpeedLock' in line:
                log(i, '|', line)
    n, first = fp_equal_from(X, Y, t0)
    check('fight: fingerprints equal', first is None, f'common ticks {n}')


def main():
    rig.DYLIB = arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/ui_test/dsrig_ui.dylib'))
    pair = rig.lease('ui', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': PORT, 'name': X, 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': Y},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        us = c(X, cmd='list_units')['units']
        unit = next(u for u in us if u['faction'] == 1 and not u['dead'])
        log(X, 'claims', unit['name'], c(X, cmd='mp_claim', guid=unit['guid']))
        time.sleep(4)
        mx, my = [os.path.getsize(os.path.join(rig.idir(i), 'rig.log')) for i in (X, Y)]
        t0 = st(X)['tick']
        log(X, 'ui_levelup', c(X, cmd='ui_levelup', unit=unit['guid'], building=STATUE, map=0))
        time.sleep(15)
        lx, ly = log_since(X, mx), log_since(Y, my)
        for i, l in ((X, lx), (Y, ly)):
            for line in l.splitlines():
                if 'ui:' in line or 'AddPauseReason' in line or 'GameSpeedLock' in line or 'level' in line.lower():
                    log(i, '|', line)
        check('level-up started by the sim on both peers', 'debug level-up: unit 0x' in lx and 'debug level-up: unit 0x' in ly)
        check(f'popup 21 opened on the owner ({X})', 'popup 21 requested' in lx and 'popup 21 not opened' not in lx)
        check(f'popup 21 not opened on {Y}', 'popup 21 not opened here' in ly)
        check('no time lock / pause reason on either peer',
              'LevelUpView time lock kept' not in lx + ly and 'AddPauseReason(4)' not in lx + ly,
              'skipped on owner' if 'LevelUpView time lock skipped' in lx else 'owner never reached SetTimeLock')
        a1, b1 = st(X), st(Y)
        time.sleep(40)
        a2, b2 = st(X), st(Y)
        check('both keep ticking (40 s)', a2['tick'] > a1['tick'] + 200 and b2['tick'] > b1['tick'] + 200,
              f"{X} {a1['tick']}->{a2['tick']}, {Y} {b1['tick']}->{b2['tick']}")
        check('nobody paused', not a2.get('mp', {}).get('paused') and not b2.get('mp', {}).get('paused'),
              f"votes {a2.get('mp', {}).get('votes')}")
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n > 300 and first is None, f'common ticks {n}, first diff {first}')
        if '--fight' in sys.argv:
            fight(X, Y, us)
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
