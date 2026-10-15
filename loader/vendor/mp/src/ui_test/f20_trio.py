#!/usr/bin/env python3
"""ux_flow F20 with 3 players (lan_v10 game lane): a map vote that arrives while the host's disconnect box is open must
wait, then open once the box closed (both boxes share the stock CommonNoticeView). Leases TWO pairs (3 copies used:
host X, guests Y + Z), hidden windows, mp.json session, base save MoonlightBrigade.
  Y and the host go on an expedition (one unit each), Z approves and stays; Z quits -> host box "Z disconnected";
  Y proposes "go down" -> host: no prompt yet ("map vote waits for the disconnect box"); host presses Keep playing ->
  the host's vote prompt opens; host votes yes -> going down.
  f20_trio.py [--dylib PATH]"""
import json, os, shutil, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import uxgame_pair as U
rig = U.rig
c, st, view, wait, mark, log_since = U.c, U.st, U.view, U.wait, U.mark, U.log_since
results = {}


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])


def lease2():
    while True:
        def take(d):
            rig._free_stale(d)
            if len(d) > int(os.environ.get('DSRIG_MAX_PAIRS', '3')) - 2: return None
            free = [pr for pr in ('AB', 'CD', 'EF') if pr not in d and not any(rig.alive(rig.read_pid(i)) for i in pr)]
            if len(free) < 2: return None
            for pr in free[:2]: d[pr] = {'lane': 'v10game', 'pid': os.getpid(), 'since': time.time(), 'used': time.time()}
            return free[:2]
        prs = rig._with_leases(take)
        if prs: return prs
        log('two free pairs needed: waiting'); time.sleep(60)


def main():
    rig.DYLIB = sys.argv[sys.argv.index('--dylib') + 1] if '--dylib' in sys.argv else os.path.expanduser('~/ds_multiplayer/build/v10game/dsrig.dylib')
    prs = lease2(); X, Y, Z = prs[0][0], prs[0][1], prs[1][0]; PORT = rig.pair_port(X)
    log('pairs', prs, 'host', X, 'guests', Y, Z, 'port', PORT)
    try:
        run(X, Y, Z, PORT)
    finally:
        for x in (X, Y, Z):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        for pr in prs: rig.release(pr)
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER (restored)')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


def run(X, Y, Z, PORT):
    assert rig.check_prefs(), 'owner prefs differ before start'
    rig.fix_mp_port = lambda x: None                           # the third copy belongs to another pair: keep OUR port in its mp.json
    json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    for g, nm in ((Y, 'Ben'), (Z, 'Cy')):
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': nm}, open(os.path.join(rig.idir(g), 'mp.json'), 'w'))
    for i in (X, Y, Z):
        d = os.path.join(rig.idir(i), 'data', 'mp_owners')
        if os.path.isdir(d): os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
        os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
    U.start(X, 1512, 982)
    wait('title host', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(X)), timeout=1500, every=5)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
    log('host load', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
    for g in (Y, Z):
        U.start(g, 1512, 982)
        wait('title ' + g, lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(g)), timeout=1500, every=5)
        wait(g + ' loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(g)), timeout=900, every=3)
        time.sleep(5)
    time.sleep(30)                                             # the host reloads its join point for the 2nd guest: let it settle
    wait('all settled', lambda: all(st(x).get('det', {}).get('locked') and not st(x).get('busy') and st(x).get('scene') == 2 for x in (X, Y, Z)), timeout=300, every=3)
    time.sleep(10)
    tm = lambda x, **k: c(x, cmd='ui_teams', **k)
    allu = wait('host units', lambda: [u['guid'] for u in c(X, cmd='list_units', map=0).get('units', []) if u['faction'] == 1 and not u['dead']], timeout=120, every=3)
    log('units', len(allu))
    tm(X, action='open'); tm(Y, action='open'); time.sleep(3)
    tm(X, action='click', guid=allu[0], button=0); time.sleep(1); tm(Y, action='click', guid=allu[1], button=0); time.sleep(2)
    tm(X, action='close'); tm(Y, action='close'); time.sleep(2)
    tm(X, action='open'); time.sleep(1); tm(X, action='go')
    wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(X)), timeout=60, every=2)
    tm(X, action='stock_select', units=f'{allu[0]},{allu[1]}'); time.sleep(1); tm(X, action='stock_next')
    wait('proposer stock step', lambda: tm(X)['stock'].get('state') == 1, timeout=60, every=2)
    tm(X, action='stock_accept')
    for g in (Y, Z):
        wait(g + ' receiver', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(g)), timeout=90, every=2)
        if tm(g).get('stock', {}).get('open'):
            tm(g, action='stock_next'); wait(g + ' summary', lambda: tm(g).get('prop', {}).get('shown'), timeout=60, every=2)
        tm(g, action='approve')
    wait('all in the dungeon', lambda: all(st(x).get('map', 0) != 0 and not st(x).get('busy') for x in (X, Y, Z)), timeout=400, every=3)
    time.sleep(10)
    mx = mark(X)
    c(Z, cmd='mp_quit'); wait('host disconnect box', lambda: view(X).get('left_prompt') == 1, timeout=60, every=1)
    alive = [u['guid'] for u in c(Y, cmd='list_units').get('units', []) if u['faction'] == 1 and not u['dead']]
    log('Ben proposes going down', c(Y, cmd='ord_inject', ev='GoDownDungeonRequested', **{'lguid@16': ','.join(alive)})); time.sleep(6)
    vx = view(X); lx = log_since(X, mx)
    check('F20: while the disconnect box is open, the host gets no vote prompt over it (it waits)', vx.get('left_prompt') == 1 and 'map vote waits for the disconnect box' in lx,
          [l for l in lx.splitlines() if 'map vote' in l or 'prompt' in l][-4:])
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(U.SHOTS, 'v10g_f20_disconnect_box_host.png'))
    c(X, cmd='ui_notice', button='cancel'); time.sleep(4)               # Keep playing
    vx = view(X); lx = log_since(X, mx)
    check('F20: after Keep playing the host\'s vote prompt opens', vx.get('left_prompt') == 0 and 'ui: map vote prompt:' in lx, [l for l in lx.splitlines() if 'map vote' in l][-3:])
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(U.SHOTS, 'v10g_f20_vote_after_box_host.png'))
    r = c(X, cmd='ui_map_vote', accept=1); time.sleep(4)
    check('F20: the host can answer the vote (prompt was really up)', r.get('rc') == 1, r)


if __name__ == '__main__':
    import signal
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(143))
    main()
