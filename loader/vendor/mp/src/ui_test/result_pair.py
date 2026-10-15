#!/usr/bin/env python3
"""(helpers shared with levelup_pair.py) Level-up popup in multiplayer, on a leased pair (CD or EF): owner rule = opens only for the unit's
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

PORT = 7811
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



def mp_owner(x, guid):
    r = c(x, cmd='ui_owner', guid=guid)
    return r.get('owner', -1)


def main():
    """Return-to-base screen (popup 3), owner rule 2026-10-12: B's unit uses the exit portal -> B picks,
    A sees it read-only and paused, B's Return travels as one order, both land on base, fingerprints equal."""
    rig.DYLIB = arg('--dylib', rig.DYLIB)
    pair = rig.lease('mpui', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    for i in (X, Y):                                              # never take over another lane's running copies
        if rig.alive(rig.read_pid(i)):
            rig.release(pair)
            raise SystemExit(f'{i} is still running (pid {rig.read_pid(i)}): not mine, pair released')
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': PORT, 'name': 'A', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'B'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):                                          # owners saved with the world by earlier runs: start clean
            d = os.path.join(rig.idir(i), 'data', 'mp_owners')
            if os.path.isdir(d):
                os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
        for i in (X, Y):                                          # settings back to the defaults (ui writes mp_ui.json)
            os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
            open(os.path.join(rig.idir(i), 'data', 'mp_ui.json'), 'w').write('{"see_through":0,"names":1,"list_open":0}\n')
        rig.start(X, fps=10, window='--window' in sys.argv)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        save = arg('--save', rig.SAVE); src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window='--window' in sys.argv)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        us = c(X, cmd='list_units')['units']
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        mine = {X: players[:2], Y: players[2:4]}
        for x in (X, Y):
            for u in mine[x]:
                log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
        time.sleep(8)
        va, vb = c(X, cmd='ui_view'), c(Y, cmd='ui_view')
        log('view A', va); log('view B', vb)
        gx, gy = [u['guid'] for u in mine[X]], [u['guid'] for u in mine[Y]]
        check('local order: own units first (A)', [g.lower() for g in va['local_order'][:2]] == [g.lower() for g in gx])
        check('local order: own units first (B)', [g.lower() for g in vb['local_order'][:2]] == [g.lower() for g in gy])
        check('bar: own units at places 1-2 on both', all(e['sibling'] < 2 for e in va['bar'] if e['owner'] == 0) and all(e['sibling'] < 2 for e in vb['bar'] if e['owner'] == 1), f"{va['bar']} {vb['bar']}")
        check('world: no see-through by default (both)', va['world_dimmed'] == 0 and vb['world_dimmed'] == 0, f"A {va['world_dimmed']}/{va['world_full']} B {vb['world_dimmed']}/{vb['world_full']}")
        check('player panel collapsed by default (header only)', va['panel'][0]['active'] == 1 and va['panel'][1]['active'] == 0 and 'Players (2)' in va['panel'][0]['text'], str(va['panel']))
        log(X, 'A opens the panel', c(X, cmd='ui_click', row=0)); log(X, 'A sets see-through', c(X, cmd='ui_click', row=2))
        time.sleep(4)
        va, vb = c(X, cmd='ui_view'), c(Y, cmd='ui_view')
        check('see-through 25% on A only: 2 other units on A, none on B', va['world_dimmed'] == 2 and vb['world_dimmed'] == 0, f"A {va['world_dimmed']} B {vb['world_dimmed']}")
        body = va['panel'][1]['text']
        check('panel rows: number, name, signal bars (no raw ping, no "you")', va['panel'][1]['active'] == 1 and '(you)' not in body and ' ms' not in body
              and body.count('1  <color=') == 1 and '2  <color=#5DADE2>B</color>' in body and '#58D68D' in body, body)
        log('panel position (top centre, under the unit bar)', va.get('panel_pos'))
        if '--window' in sys.argv:
            time.sleep(3)
            cap = os.path.join(rig.idir(X), 'ui_capture.png')
            log(X, 'capture', c(X, cmd='ui_capture', path=cap), cap)
            capb = os.path.join(rig.idir(Y), 'ui_capture.png')
            log(Y, 'capture', c(Y, cmd='ui_capture', path=capb), capb)
        # admin mode: host orders B's unit -> refused while off, applied while on (same on both peers)
        def ords(x): m = st(x).get('mp', {}); return m.get('orders', 0), m.get('orders_dropped_owner', 0)
        tgt = gy[0]; cellb = [u for u in us if u['guid'] == tgt][0]['cell']
        oa, ob = ords(X), ords(Y)
        rig.send(X, {'cmd': 'mp_move', 'guid': tgt, 'x': cellb[0] + 1, 'y': cellb[1]}, timeout=60); time.sleep(4)
        na, nb2 = ords(X), ords(Y)
        check('admin off: host order on B unit dropped on both', na[1] == oa[1] + 1 and nb2[1] == ob[1] + 1 and na[0] == oa[0], f'{oa}->{na} {ob}->{nb2}')
        log(X, 'host turns admin mode on', c(X, cmd='ui_click', row=4)); time.sleep(4)
        aa, ab = c(X, cmd='ui_view')['admin'], c(Y, cmd='ui_view')['admin']
        oa, ob = ords(X), ords(Y)
        rig.send(X, {'cmd': 'mp_move', 'guid': tgt, 'x': cellb[0] + 1, 'y': cellb[1]}, timeout=60); time.sleep(4)
        na, nb2 = ords(X), ords(Y)
        check('admin on (both peers): host order on B unit applied on both', aa == 1 and ab == 1 and na[0] == oa[0] + 1 and nb2[0] == ob[0] + 1 and na[1] == oa[1], f'admin {aa}/{ab} {oa}->{na} {ob}->{nb2}')
        c(X, cmd='ui_click', row=4); time.sleep(3)
        check('admin off again on both', c(X, cmd='ui_view')['admin'] == 0 and c(Y, cmd='ui_view')['admin'] == 0)
        r = c(Y, cmd='ui_click', row=4); time.sleep(3)
        check('guest cannot turn admin mode on', c(X, cmd='ui_view')['admin'] == 0, str(r))
        check('settings saved', va['settings'] == {'see_through': 25, 'names': 1, 'list_open': 1}, str(va['settings']))
        # A looks at B's unit: status screen shows the "Owned by B" banner, buttons disabled
        inject(X, 'SelectPlayerUnitRequested', i32_16=-1, guid_20=gy[0]); time.sleep(1)
        inject(X, 'ToggleEntityUIRequested', i32_16=6); time.sleep(3)
        va = c(X, cmd='ui_view')
        check('banner "Owned by B" on A for B unit', any('Owned by' in t and 'B' in t for t in va['banner']) and va['disabled_buttons'] > 0, f"{va['banner']} disabled {va['disabled_buttons']} foreign {va.get('dbg_foreign')} views {va.get('dbg_views')}")
        inject(X, 'SelectPlayerUnitRequested', i32_16=-1, guid_20=gx[0]); time.sleep(3)
        va = c(X, cmd='ui_view')
        check('own unit: no banner, buttons back', not any(va['banner']) and va['disabled_buttons'] == 0, f"{va['banner']} {va['disabled_buttons']}")
        inject(X, 'ToggleEntityUIRequested', i32_16=6)
        # select all on B (local event): only B's units end up selected
        r = c(Y, cmd='ord_inject', ev='SelectAllUnitRequested'); time.sleep(2)
        sel = [g.lower() for g in c(Y, cmd='ord_stats').get('sel', [])]
        check('select all on B selects only B units', r.get('ok') and sorted(sel) == sorted(g.lower() for g in gy), f'inject {r} sel {sel}')
        marks = {i: os.path.getsize(os.path.join(rig.idir(i), 'rig.log')) for i in (X, Y)}
        if '--walk' in sys.argv:                                  # walk into the portal cell (needs a cursor option)
            pc = [int(v) for v in arg('--portal', '459,193').split(',')]
            for u in mine[Y]:
                log(Y, u['name'][:10], '-> portal', pc, order_move(Y, u['guid'], pc))
        else:                                                     # the sim's own portal use, same step on both
            bl = c(Y, cmd='ui_buildings', map=1, subtype=2)
            log(Y, 'dungeon portals', bl)
            portal = bl['buildings'][0]['guid']
            log(Y, 'B unit uses the portal', c(Y, cmd='ui_interact', unit=mine[Y][0]['guid'], building=portal, map=1))
        rs = wait('return screen open on both', lambda: (lambda a, b: (a, b) if a.get('open') and b.get('open') else None)(c(X, cmd='ui_result'), c(Y, cmd='ui_result')), timeout=900, every=3)
        log('result state', rs)
        for i in (X, Y):
            for line in log_since(i, marks[i]).splitlines():
                if 'ui: return screen' in line or 'ui: popup 3' in line:
                    log(i, '|', line)
        ra, rb = rs
        check('same chooser on both peers', ra['chooser'] == rb['chooser'], f'{ra} {rb}')
        P, R = (X, Y) if ra['chooser'] == 0 else (Y, X)          # picker, read-only
        log('picker', P, 'read-only', R)
        time.sleep(6)
        a1, b1 = st(X), st(Y); time.sleep(8); a2, b2 = st(X), st(Y)
        check('both paused while the screen is open', a2['tick'] == a1['tick'] and b2['tick'] == b1['tick'] and a2['tick'] == b2['tick'],
              f"A {a1['tick']}->{a2['tick']} B {b1['tick']}->{b2['tick']} votes {a2.get('mp', {}).get('votes')}")
        va, vb = c(X, cmd='ui_view'), c(Y, cmd='ui_view')
        check('pause panel shown with the same line on both', va['pause_panel'] == 1 and vb['pause_panel'] == 1 and va['pause_line'] == vb['pause_line'] and 'Paused by' in va['pause_line'], f"A {va['pause_panel']} '{va['pause_line']}' B {vb['pause_panel']} '{vb['pause_line']}'")
        log(R, 'read-only side presses Return (must be blocked)', c(R, cmd='ui_result', action='return'))
        time.sleep(3)
        mx = os.path.getsize(os.path.join(rig.idir(X), 'rig.log'))
        check('read-only side: its Return click is blocked', 'return screen click blocked' in log_since(R, marks[R]) and c(R, cmd='ui_result')['open'] == 1)
        log(P, 'picker presses Return', c(P, cmd='ui_result', action='return'))
        time.sleep(3)
        for k in range(3):                                        # the screen may ask twice (units left behind)
            r = c(P, cmd='ui_result', action='confirm'); log(P, 'picker confirms', k + 1, r)
            time.sleep(4)
            if not c(P, cmd='ui_result')['open']:
                break
        land = wait('both on the base map', lambda: (lambda a, b: (a, b) if a.get('map') == 0 and b.get('map') == 0 and not a.get('busy') and not b.get('busy') else None)(st(X), st(Y)), timeout=900, every=3)
        for i in (X, Y):
            for line in log_since(i, mx if i == X else marks[i]).splitlines():
                if 'ui:' in line or 'handed to' in line or 'ChangeMap' in line:
                    log(i, '|', line)
        check('both landed on base', True, f"A tick {land[0]['tick']} B tick {land[1]['tick']}")
        t0 = max(land[0]['tick'], land[1]['tick'])
        time.sleep(5)
        mk = {i: os.path.getsize(os.path.join(rig.idir(i), 'rig.log')) for i in (X, Y)}
        # the expedition window is local UI (opened from the base portal panel): ShowPopUpRequested(ExpeditionPlan 2).
        # B claims one unclaimed base unit first; then each side must list only the units it may order.
        base = [u for u in c(Y, cmd='list_units', map=0)['units'] if u['faction'] == 1 and not u['dead']]
        own = {u['guid'].lower(): mp_owner(Y, u['guid']) for u in base}
        free = [u for u in base if own[u['guid'].lower()] < 0]
        if free:
            log(Y, 'B claims base unit', free[0]['name'], c(Y, cmd='mp_claim', guid=free[0]['guid'])); time.sleep(4)
        own = {u['guid'].lower(): mp_owner(Y, u['guid']) for u in base}
        nb = sum(1 for o in own.values() if o == 1); na = sum(1 for o in own.values() if o in (0, -1)); n = len(base)
        log('base units', n, 'B owns', nb, 'A may order', na)
        for who, exp in ((Y, nb), (X, na)):
            mk = os.path.getsize(os.path.join(rig.idir(who), 'rig.log'))
            log(who, 'opens the expedition window', inject(who, 'ShowPopUpRequested', i32_16=2)); time.sleep(6)
            lg = log_since(who, mk)
            shown = f'expedition window shows {exp} of {n}' in lg or (exp == n and 'expedition window shows' not in lg and 'popup 2 requested' in lg)
            check(f'expedition window on {who} lists only its own units ({exp} of {n})', shown and nb >= 1,
                  [l for l in lg.splitlines() if 'expedition' in l or 'popup 2' in l])
            inject(who, 'ClosePopUpRequested', i32_16=2); time.sleep(2)
        wait('1000 ticks after landing on both', lambda: min(st(X)['tick'], st(Y)['tick']) > t0 + 1000, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal 1000+ ticks after landing', n >= 1000 and first is None, f'common ticks {n}, first diff {first}')
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        ok = results and all(results.values()) and sys.exc_info()[0] is None
        log('RESULT', 'PASS' if ok else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
