#!/usr/bin/env python3
"""lan_v10 game lane: co-op markers, colours, see-through, outline, story countdown (research/ping_camera_v10.md,
PLAN.md "Owner answers (2026-10-14 01:40)", owner ping override 01:55). One leased pair, hidden windows 1512x982,
mp.json session (host the owner, guest Ben), base save MoonlightBrigade (uxgame_pair.py setup). Frames:
research/ux_shots/v10_*.png. Every step logs PASS / FAIL; the run goes on after a failure.
  coop_pair.py [--dylib PATH]          (default build/v10game/dsrig.dylib)
  COOPONLY=ping,views ...              only these steps (setup always runs)"""
import json, os, sys, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import uxgame_pair as U                                        # start / c / st / view / wait / mark / log_since / fp_equal_from_xy
rig = U.rig
SHOTS = U.SHOTS
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, str(info)[:700])


c, st, view, wait, mark, log_since = U.c, U.st, U.view, U.wait, U.mark, U.log_since


def coop(inst, **k):
    try:
        return rig.send(inst, dict(cmd='ui_coop', **k), timeout=120)
    except Exception as e:
        return {'ok': False, 'error': repr(e)}


def shot(x, name):
    p = os.path.join(SHOTS, f'v10g_{name}.png')
    r = c(x, cmd='mpm_shot', how='backbuffer', path=p)
    log('frame', p, r.get('ok'))
    return p


def step(name, fn):
    only = os.environ.get('COOPONLY')
    if only and name not in only.split(','):
        return
    log('--- step', name)
    try:
        fn()
    except Exception:
        check('step ' + name + ' ran', False, traceback.format_exc()[-700:])


def lease():
    while True:
        def take(d):
            rig._free_stale(d)
            if len(d) >= int(os.environ.get('DSRIG_MAX_PAIRS', '3')): return None
            for pr in ('AB', 'CD', 'EF'):
                if pr not in d and not any(rig.alive(rig.read_pid(i)) for i in pr):
                    d[pr] = {'lane': 'v10game', 'pid': os.getpid(), 'since': time.time(), 'used': time.time()}; return pr
        pr = rig._with_leases(take)
        if pr: return pr
        log('no free pair: waiting'); time.sleep(60)


def main():
    rig.DYLIB = sys.argv[sys.argv.index('--dylib') + 1] if '--dylib' in sys.argv else os.path.expanduser('~/ds_multiplayer/build/v10game/dsrig.dylib')
    pair = lease(); X, Y = pair[0], pair[1]; PORT = rig.pair_port(X)
    log('pair', pair, 'port', PORT, 'dylib', rig.DYLIB)
    try:
        run(X, Y, PORT)
    finally:
        for x in (X, Y):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        rig.release(pair)
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER (restored)')
        ok = results and all(results.values())
        log('RESULT', 'PASS' if ok else 'FAIL', json.dumps(results))


def run(X, Y, PORT):
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
    for i in (X, Y):
        d = os.path.join(rig.idir(i), 'data', 'mp_owners')
        if os.path.isdir(d): os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
        os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
        # an old file with see-through 0: lan_v10 reads it as the 30 % minimum
        open(os.path.join(rig.idir(i), 'data', 'mp_ui.json'), 'w').write('{"see_through":0,"names":1,"list_open":1}\n')
    U.start(X, 1512, 982)
    wait('title host', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(X)), timeout=1500, every=5)
    mk = {X: mark(X)}
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    import shutil; src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
    log('host load', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
    U.start(Y, 1512, 982)
    wait('title guest', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(Y)), timeout=1500, every=5)
    mk[Y] = mark(Y)
    wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900, every=3)
    time.sleep(8)
    t_fp = st(X).get('tick', 0)
    S = {}

    def probe():
        for m in ('arrow', 'triangle', 'pointer', 'white'):
            r = c(X, cmd='ui_sprites', match=m)
            log(f'sprites "{m}":', r.get('names', [])[:60])
        a, b = coop(X), coop(Y)
        log('coop host', a); log('coop guest', b)
        check('coop: both peers see the same map key, markers on', a.get('coop_ok') == 1 and b.get('coop_ok') == 1 and a.get('key') == b.get('key'), f"{a.get('key')} {b.get('key')}")
        check('see-through: an old 0 reads as the 30 % minimum', a.get('see_through') == 30 and b.get('see_through') == 30, f"{a.get('see_through')} {b.get('see_through')}")
        S['net'] = a.get('net') == 1 and b.get('net') == 1
        log('dsnet marker API present:', S['net'])
    step('probe', probe)

    def units(x):
        return [u for u in c(x, cmd='list_units', map=0).get('units', []) if u['faction'] == 1 and not u['dead'] and 'cell' in u]

    def ping():
        us = units(X); u0 = us[0]; cx, cy = u0['cell']
        coop(X, action='camera', x=cx + 0.5, y=cy + 0.5); coop(Y, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(2)
        far = (cx + 5, cy + 3)
        occupied = {tuple(u['cell']) for u in us}
        while any(abs(far[0] - a) <= 1 and abs(far[1] - b) <= 1 for a, b in occupied): far = (far[0] + 1, far[1])
        S['u0'], S['far'] = u0, far
        mx = mark(X); S['mark_ping'] = (mx, mark(Y))
        r = coop(X, action='ping', x=far[0], y=far[1]); time.sleep(2)
        a, b = coop(X), coop(Y)
        ph = [p for p in a.get('pings', []) if tuple(p['cell']) == far]
        check('ping: placed on the host, on screen, own colour (inside 0)', r.get('rc') == 0 and ph and ph[0]['on_screen'] == 1 and ph[0]['inside'] == 0, a.get('pings'))
        if S['net']:
            pg = [p for p in b.get('pings', []) if tuple(p['cell']) == far]
            check('ping: the guest sees it (P_MARK, outside the sim)', bool(pg) and pg[0]['owner'] == 0 and pg[0]['on_screen'] == 1, b.get('pings'))
        shot(X, 'ping_host'); shot(Y, 'ping_guest')
        # a ping next to a unit: that unit does not clear it
        near = (cx + 1, cy)
        coop(X, action='ping', x=near[0], y=near[1]); time.sleep(3)
        a = coop(X); pn = [p for p in a.get('pings', []) if tuple(p['cell']) == near]
        check('ping next to a unit: stays (the unit was there when it was placed)', bool(pn) and pn[0]['inside'] >= 1, a.get('pings'))
        # B over it deletes it (toggle), on both
        coop(X, action='toggle', x=near[0], y=near[1]); time.sleep(2)
        a, b = coop(X), coop(Y)
        check('B over a ping deletes it (host)', not [p for p in a.get('pings', []) if tuple(p['cell']) == near], a.get('pings'))
        if S['net']:
            check('... and on the guest', not [p for p in b.get('pings', []) if tuple(p['cell']) == near], b.get('pings'))
        # the guest pings too; the host sees it in the guest's colour
        if S['net']:
            g2 = (far[0], far[1] + 3)
            coop(Y, action='ping', x=g2[0], y=g2[1]); time.sleep(2)
            a = coop(X); pa = [p for p in a.get('pings', []) if tuple(p['cell']) == g2]
            check("guest's ping shows on the host (owner 1)", bool(pa) and pa[0]['owner'] == 1, a.get('pings'))
            S['g2'] = g2
            time.sleep(1); shot(X, 'ping_two_host')
            pid = pa[0]['id'] if pa else 0
            coop(X, action='camera', x=g2[0] - 30.5, y=g2[1] - 10.5); time.sleep(2); b0 = coop(X)['cam_real']
            r = coop(X, action='chatlink', player=1, x=pid); time.sleep(2); a = coop(X)
            # (the camera stops at the map's edge: within 3 cells of the ping, and it moved there)
            check('chat line "Ben pinged a spot." click: the camera goes to the ping', r.get('rc') == 0 and abs(a['cam_real'][0] - (g2[0] + 0.5)) < 3 and abs(a['cam_real'][1] - (g2[1] + 0.5)) < 3
                  and abs(a['cam_real'][0] - b0[0]) > 5, f"rc {r.get('rc')} before {b0} after {a.get('cam_real')}")
            lx = log_since(X, S['mark_ping'][0])
            check('host chat shows "Ben pinged a spot." (mpmenu line)', 'ui: ping received by 1' in lx, [l for l in lx.splitlines() if 'received' in l][:2])
        lx = log_since(X, mx)
        check('ping: logged as placed, no error', 'ui: ping placed by 0' in lx, [l for l in lx.splitlines() if 'ping' in l][:6])
    step('ping', ping)

    def arrival():
        u0, far = S['u0'], S['far']
        mx, my = S['mark_ping']
        U.move_order(X, u0['guid'], far); time.sleep(12)
        a, b = coop(X), coop(Y)
        ux = [u for u in units(X) if u['guid'] == u0['guid']]
        log('unit now at', ux[0]['cell'] if ux else None)
        check('a unit reaches the ping: cleared on the host', not [p for p in a.get('pings', []) if tuple(p['cell']) == tuple(far)], a.get('pings'))
        if S['net']:
            check('... and on the guest (its own copy of the unit positions)', not [p for p in b.get('pings', []) if tuple(p['cell']) == tuple(far)], b.get('pings'))
        lx = log_since(X, mx)
        check('arrival logged "a unit reached it"', 'a unit reached it' in lx, [l for l in lx.splitlines() if 'ping' in l][:6])
    step('arrival', arrival)

    def spam():
        mx = mark(X)
        u0 = S['u0']; bx, by = u0['cell'][0] - 8, u0['cell'][1] - 8
        rcs = [coop(X, action='ping', x=bx - 2 * k, y=by).get('rc') for k in range(4)]
        lx = log_since(X, mx)
        check('spam limit: 3 in 5 s, the 4th refused with the notice', rcs[:3] == [0, 0, 0] and rcs[3] == -2 and 'Wait a moment before you ping again.' in lx, rcs)
        a = coop(X)
        check('max 3 live pings per player (older ones replaced)', sum(1 for p in a.get('pings', []) if p['owner'] == 0) <= 3, a.get('pings'))
    step('spam', spam)

    def arrows():
        time.sleep(6)                                          # spam lock over
        u0 = S['u0']; cx, cy = u0['cell']
        coop(X, action='ping', x=cx - 45, y=cy - 30); time.sleep(1)    # far down-left of the view: an edge arrow
        coop(X, action='ping', x=cx, y=cy + 2); time.sleep(1)
        a = coop(X); ar = [p for p in a.get('pings', []) if p['arrow'] == 1]
        log('camera', a.get('cam'), 'pings', a.get('pings'))
        check('off-screen ping: an edge arrow instead of the ring', bool(ar), a.get('pings'))
        shot(X, 'ping_offscreen_arrow')
        r = c(X, cmd='ui_tip_hover', title="Your Ping"); log('tip hover', r)
        coop(X, action='camera', x=cx + 20.5, y=cy + 10.5); time.sleep(1)
        coop(X, action='cycle'); time.sleep(2)
        a = coop(X)
        check('Shift + B: the camera goes to the newest ping', abs(a['cam'][0] - (cx + 0.5)) < 1.5 and abs(a['cam'][1] - (cy + 2.5)) < 1.5, a.get('cam'))
    step('arrows', arrows)

    def views():
        if not S['net']:
            check('views: dsnet P_VIEW present', False, 'dsnet_view_send missing in this build'); return
        u0 = S['u0']; cx, cy = u0['cell']
        coop(Y, action='camera', x=cx + 0.5, y=cy + 0.5); coop(X, action='camera', x=cx + 6.5, y=cy + 2.5); time.sleep(4)
        b = coop(Y); vh = [v for v in b.get('views', []) if v['player'] == 0]
        check("views: the guest knows the host's view (P_VIEW) and draws its corners", bool(vh) and abs(vh[0]['view'][0] - (cx + 6.5)) < 0.3 and vh[0]['brackets'] == 1, b.get('views'))
        shot(Y, 'views_guest')
        # Players panel row click = look where the host looks
        coop(Y, action='look', player=0); time.sleep(2)
        b = coop(Y)
        check("look at a friend's view: the guest camera goes there (target and the real camera)", abs(b['cam'][0] - (cx + 6.5)) < 0.3 and abs(b['cam'][1] - (cy + 2.5)) < 0.3
              and abs(b['cam_real'][0] - (cx + 6.5)) < 0.5 and abs(b['cam_real'][1] - (cy + 2.5)) < 0.5, f"{b.get('cam')} real {b.get('cam_real')}")
        # follow: the host moves, the guest follows; the guest moves its own camera: the follow stops
        coop(Y, action='follow', player=0); time.sleep(1)
        coop(X, action='camera', x=cx + 12.5, y=cy - 3.5); time.sleep(3)
        b = coop(Y)
        check('follow: the guest camera follows the host (real camera too)', b.get('follow') == 0 and abs(b['cam'][0] - (cx + 12.5)) < 0.3 and abs(b['cam_real'][0] - (cx + 12.5)) < 0.5, f"{b.get('cam')} real {b.get('cam_real')}")
        my = mark(Y)
        coop(Y, action='camera', x=cx - 10.5, y=cy + 0.5); time.sleep(2)
        b = coop(Y)
        check('own camera move stops the follow ("Stopped following the owner.")', b.get('follow') == -1 and 'Stopped following the owner.' in log_since(Y, my), b.get('follow'))
        r = coop(Y, action='menu', player=0); time.sleep(1); shot(Y, 'player_menu_guest')
        if not r.get('menu'): log('menu reply', r); r = coop(Y); r['rc'] = 0   # run 14: one reply came back without "menu" (rows were on screen): read the state again
        check('player menu: Follow + Mute rows', r.get('rc') == 0 and any('Follow' in m for m in r.get('menu', [])) and any('Mute' in m for m in r.get('menu', [])), r.get('menu'))
        c(Y, cmd='ui_escape')
        coop(Y, action='zoom', x=5); coop(X, action='zoom', x=5); coop(Y, action='camera', x=cx - 30.5, y=cy - 20.5); coop(X, action='camera', x=cx + 30.5, y=cy + 20.5); time.sleep(4)   # small guest view, far from the host's
        coop(Y, action='set', key='offscreen', value=1); time.sleep(3)
        b = coop(Y); log('views with off-screen marks', b.get('views'))
        check('off-screen friend mark (option on)', any(v.get('edge') == 1 for v in b.get('views', [])), b.get('views'))
        shot(Y, 'views_offscreen_guest'); coop(Y, action='zoom', x=17); coop(X, action='zoom', x=17)
    step('views', views)

    def mute():
        if not S['net']: return
        coop(X, action='mute', player=1); time.sleep(1)
        u0 = S['u0']; cx, cy = u0['cell']
        coop(Y, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(5)
        coop(Y, action='ping', x=cx + 3, y=cy + 5); time.sleep(2)
        a = coop(X); pm = [p for p in a.get('pings', []) if tuple(p['cell']) == (cx + 3, cy + 5)]
        check("mute: the host keeps Ben's ping but draws nothing (on_screen 0, arrow 0)", bool(pm) and pm[0]['on_screen'] == 0 and pm[0]['arrow'] == 0, pm)
        coop(X, action='unmute', player=1)
    step('mute', mute)

    def colours():
        h0 = c(X, cmd='mp_colors'); g0 = c(Y, cmd='mp_colors')
        log('colours host view', h0, 'guest view', g0)
        coop(X, action='color', player=1, x=1); time.sleep(1)
        h1 = c(X, cmd='mp_colors'); g1 = c(Y, cmd='mp_colors')
        col = lambda r, s: next((q['color'] for q in r.get('slots', []) if q['slot'] == s), None)
        check("override: the host's choice for Ben changes Ben's colour on the host only", col(h1, 1) != col(h0, 1) and col(g1, 1) == col(g0, 1), f'host {col(h0,1)}->{col(h1,1)} guest {col(g0,1)}->{col(g1,1)}')
        pal = ['#FFEE88', '#44AAFF', '#DD66BB', '#EEF6FF', '#AA9988', '#DDAA00', '#BBBBCC', '#AACCFF']
        check('auto colours come from the colour-blind-safe palette', col(h0, 0) in pal and col(h0, 1) in pal, f'{col(h0,0)} {col(h0,1)}')
        js = open(os.path.join(rig.idir(X), 'data', 'mp_ui.json')).read()
        check('override saved locally (mp_ui.json colors)', '"colors":"' in js and len(js.split('"colors":"')[1].split('"')[0]) > 30, js[-120:])
        c(X, cmd='ui_pop', open=1); time.sleep(2); shot(X, 'cog_rows_host')
        r = c(X, cmd='ui_tip_hover', title='Show Pings'); time.sleep(1.5); r = c(X, cmd='ui_tip_hover', title='Show Pings')
        check('tooltip "Show Pings" in the game tooltip window', r.get('shown') == 'Show Pings', r); shot(X, 'tip_show_pings')
        c(X, cmd='ui_tip_hover', title=''); c(X, cmd='ui_pop', open=0)
    step('colours', colours)

    def outline():
        u0 = S['u0']; cx, cy = u0['cell']
        allu = [u['guid'] for u in units(X)]                 # host takes 2, Ben takes 2 (Teams window, as players do)
        c(X, cmd='ui_teams', action='open'); c(Y, cmd='ui_teams', action='open'); time.sleep(3)
        for u in allu[:2]: c(X, cmd='ui_teams', action='click', guid=u, button=0); time.sleep(1)
        for u in allu[2:4]: c(Y, cmd='ui_teams', action='click', guid=u, button=0); time.sleep(1)
        S['claimed'] = allu[:4]
        c(X, cmd='ui_teams', action='close'); c(Y, cmd='ui_teams', action='close'); time.sleep(3)
        coop(X, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(1)
        coop(X, action='set', key='outline', value=3); time.sleep(8)
        a = coop(X)
        check('outline Both: own 2 + Ben 2 outlined on the host', a.get('outlined', 0) >= 4 and a.get('outline') == 3, a.get('outlined'))
        coop(Y, action='set', key='outline', value=2)
        try: b = wait('guest outline', lambda: (lambda r: r if r.get('outlined') == 2 else None)(coop(Y)), timeout=20, every=2)
        except RuntimeError: b = coop(Y)
        check("outline Friends' units on the guest: the host's 2", b.get('outlined') == 2, f"{b.get('outlined')} (owners: {[c(Y, cmd='ui_owner', guid=g).get('owner') for g in S.get('claimed', [])]})")
        shot(Y, 'outline_friends_guest'); coop(Y, action='set', key='outline', value=0)
        shot(X, 'outline_both_host')
        coop(X, action='set', key='outline', value=0); time.sleep(8)
        a = coop(X)
        check('outline Off: none left', a.get('outlined') == 0, a.get('outlined'))
    step('outline', outline)

    def story():
        mx, my = mark(X), mark(Y)
        log('rule FIRST', c(X, cmd='ui_story', action='rule', value=2)); time.sleep(2)
        log('event', c(X, cmd='ui_story', action='event', key='Event_Random4'))
        wait('story popup on both', lambda: c(X, cmd='ui_story').get('active') == 1 and c(Y, cmd='ui_story').get('active') == 1, timeout=60, every=2)
        time.sleep(3); shot(Y, 'story_countdown_guest')
        lt = c(Y, cmd='ui_texts', min=10)
        tl = [t['text'] for t in lt.get('texts', []) if 'then' in t['text'] and 'decide' in t['text']]
        check('story: the countdown shows on the guest ("... s, then the owner decides")', bool(tl), tl[:2])
        time.sleep(62)
        s = c(Y, cmd='ui_story'); lt = c(Y, cmd='ui_texts', min=10)
        tl = [t['text'] for t in lt.get('texts', []) if 'no answer in 60 s' in t['text']]
        check('story: after 60 s the host decides (title on the guest)', bool(tl) and s.get('chooser') == 0, f"chooser {s.get('chooser')} {tl[:1]}")
        shot(Y, 'story_handover_guest')
        c(Y, cmd='ui_story', action='click', idx=0); time.sleep(2)
        s = c(Y, cmd='ui_story'); check("story: the guest's late click does not decide", s.get('decided', -1) < 0, s)
        c(X, cmd='ui_story', action='click', idx=0); time.sleep(4)
        lx = log_since(X, mx)
        check('story: the host decides (choice applied, next node or end)', 'host sends story choice 0' in lx, [l for l in lx.splitlines() if 'story' in l][-4:])
        for _ in range(4):                                    # the event may have more nodes: the host answers them
            if c(X, cmd='ui_story').get('active') != 1 and c(Y, cmd='ui_story').get('active') != 1: break
            c(X, cmd='ui_story', action='click', idx=0); time.sleep(5)
        sx, sy = c(X, cmd='ui_story'), c(Y, cmd='ui_story')
        check('story: the event ends on both', sx.get('active') == 0 and sy.get('active') == 0, f"{sx.get('active')} {sy.get('active')}")
        c(X, cmd='ui_story', action='rule', value=0)
    step('story', story)

    def f18():                                                # ux_flow F18: Release from the lend menu says it
        mine = [u for u in units(X) if c(X, cmd='ui_owner', guid=u['guid']).get('owner') == 0]
        if not mine: check('F18: the host owns a unit to release', False, 'none'); return
        g = mine[0]['guid']; mx = mark(X)
        r = c(X, cmd='ui_lend', guid=g, action='menu'); log('lend menu', r.get('menu'))
        k = next((i for i, m in enumerate(r.get('menu', [])) if 'Release' in m), None)
        if k is None: check('F18: Release row in the lend menu', False, r.get('menu')); return
        c(X, cmd='ui_lend', row=k, action='row'); time.sleep(3)   # ("row" key first: jnum takes the first "row")
        lx = log_since(X, mx)
        check('F18: "You release <unit>" after Release in the lend menu', 'ui: You release' in lx and c(X, cmd='ui_owner', guid=g).get('owner') == -1, [l for l in lx.splitlines() if 'release' in l.lower()][:3])
    step('f18', f18)


    def mapflag():                                             # lead: the "map change running" flag never sticks
        mx = mark(X)
        coop(X, action='mapchanging'); time.sleep(1)
        a = coop(X); log('flag set', a.get('map_changing'))
        mine = [u for u in units(X) if c(X, cmd='ui_owner', guid=u['guid']).get('owner') == 0]
        if mine:
            g = mine[0]; U.move_order(X, g['guid'], (g['cell'][0] + 1, g['cell'][1])); time.sleep(2)
        lx = log_since(X, mx)
        blocked = 'map change running' in lx
        try: wait('flag cleared', lambda: coop(X).get('map_changing') == 0, timeout=30, every=2)
        except RuntimeError: pass
        a = coop(X); lx = log_since(X, mx)
        check('map change flag: set -> orders wait; no map change -> cleared within 30 s, holds released', a.get('map_changing') == 0
              and 'ended with no map change' in lx and (blocked or not mine), [l for l in lx.splitlines() if 'map change' in l][:4])
        if mine:
            g2 = [u for u in units(X) if u['guid'] == mine[0]['guid']][0]; tgt = (g2['cell'][0] + 2, g2['cell'][1])
            U.move_order(X, g2['guid'], tgt); time.sleep(8)
            g3 = [u for u in units(X) if u['guid'] == mine[0]['guid']][0]
            check('map change flag: orders work again after it cleared', g3['cell'] != g2['cell'], f"{g2['cell']} -> {g3['cell']}")
    step('mapflag', mapflag)

    def slide():                                               # lead: an arrow that would sit behind the portrait bar slides off it
        u0 = S['u0']; cx, cy = u0['cell']
        coop(X, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(4)
        coop(X, action='ping', x=cx + 20, y=cy + 60); time.sleep(3)
        a = coop(X); pa = [p for p in a.get('pings', []) if tuple(p['cell']) == (cx + 20, cy + 60)]
        rects = a.get('avoid', [])
        log('arrow', pa, 'avoid rects', rects)
        if not pa or not pa[0]['arrow']: check('slide: the arrow shows', False, pa); return
        x, y = pa[0]['arrow_at']
        raw_x = 756 + (pa[0]['screen'][0] - 756) * (958 - 491) / max(1, pa[0]['screen'][1] - 491)   # unslid top-edge point (1512x982, 24 margin)
        inside_raw = any(r[0] <= raw_x <= r[2] and r[1] <= 958 <= r[3] for r in rects)
        clear = not any(r[0] - 16 < x < r[2] + 16 and r[1] - 16 < y < r[3] + 16 for r in rects)
        check('slide: the top-edge place is behind the portrait bar, the arrow slid clear of every box', inside_raw and clear, f'raw x {raw_x:.0f}, arrow at {x:.0f},{y:.0f}')
        shot(X, 'edge_arrow_slid_off_bar')
    step('slide', slide)

    def f13():                                                 # ux_flow F13: the viewer's "Ask to lend" has a 10 s cooldown
        mine = [u['guid'] for u in units(X) if c(X, cmd='ui_owner', guid=u['guid']).get('owner') == 0]
        if not mine:
            free = [u['guid'] for u in units(X) if c(X, cmd='ui_owner', guid=u['guid']).get('owner') == -1]
            c(X, cmd='ui_teams', action='open'); time.sleep(2); c(X, cmd='ui_teams', action='click', guid=free[0], button=0); time.sleep(2); c(X, cmd='ui_teams', action='close'); mine = [free[0]]
        for x in (X, Y):
            if view(x).get('esc_open'): c(x, cmd='ord_inject', ev='ToggleSystemMenuRequested'); time.sleep(1)
        tm = lambda x, **k: c(x, cmd='ui_teams', **k)
        tm(X, action='open'); time.sleep(1); tm(X, action='go')
        wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(X)), timeout=60, every=2)
        tm(X, action='stock_select', units=','.join(mine[:1])); time.sleep(1); tm(X, action='stock_next')
        wait('proposer stock step', lambda: tm(X)['stock'].get('state') == 1, timeout=60, every=2)
        tm(X, action='stock_accept')
        wait('receiver summary', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(Y)), timeout=90, every=2)
        if tm(Y).get('stock', {}).get('open'): tm(Y, action='stock_next'); wait('summary', lambda: tm(Y).get('prop', {}).get('shown'), timeout=60, every=2)
        tm(Y, action='approve')
        wait('both in the dungeon', lambda: (lambda a, b: a.get('map', 0) != 0 and b.get('map', 0) != 0 and not a.get('busy') and not b.get('busy'))(st(X), st(Y)), timeout=400, every=3)
        time.sleep(10)
        v = c(Y, cmd='ui_viewer'); log('guest viewer', v)
        my = mark(Y)
        c(Y, cmd='ui_viewer', action='ask'); time.sleep(1); c(Y, cmd='ui_viewer', action='ask'); time.sleep(2)
        ly = log_since(Y, my)
        check('F13: guest is a viewer; a 2nd ask within 10 s says "You asked the owner a moment ago"', v.get('viewer') == 1 and 'Asked the owner to lend you a unit' in ly and 'You asked the owner a moment ago' in ly,
              [l for l in ly.splitlines() if 'sk' in l][:4])
        shot(Y, 'f13_viewer_ask_cooldown')
    step('f13', f13)

    def edge_hidden():                                         # lead: no edge arrow over a game window
        u0 = S['u0']; cx, cy = u0['cell']
        coop(X, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(6)
        coop(X, action='ping', x=cx - 45, y=cy - 30); time.sleep(2)
        a = coop(X); before = [p['arrow'] for p in a.get('pings', []) if tuple(p['cell']) == (cx - 45, cy - 30)]
        c(X, cmd='mpm_esc'); time.sleep(3)
        a = coop(X); during = [p['arrow'] for p in a.get('pings', []) if tuple(p['cell']) == (cx - 45, cy - 30)]
        shot(X, 'edge_hidden_esc_menu')
        for _ in range(3):                                     # close the Esc menu (as the Escape key)
            if not view(X).get('esc_open'): break
            c(X, cmd='mpm_escape'); time.sleep(2)
        log('Esc menu open after close:', view(X).get('esc_open'))
        check('edge arrow shown on the map, hidden while the Esc menu is open', before and all(before) and during and not any(during), f'{before} {during}')
        shot(X, 'edge_arrow_clear_of_boxes')
    step('edge', edge_hidden)

    def fingerprints():
        time.sleep(5)
        n, first = U.fp_equal_from_xy(X, Y, t_fp)
        check('fingerprints equal over the whole run (pings, views, follow outside the sim)', first is None and n > 100, f'common ticks {n}, first diff {first}')
        bad = [l for i in (X, Y) for l in log_since(i, mk[i]).splitlines() if 'TYPE CHECK' in l]
        check('no type-check skips', not bad, bad[:5])
    step('fingerprints', fingerprints)

    def f10():                                                # ux_flow F10: the host's wait line: name, time, Play = keep playing
        c(Y, cmd='mp_quit'); wait('host prompt', lambda: view(X).get('left_prompt') == 1, timeout=60, every=1)
        c(X, cmd='ui_notice', button='confirm'); time.sleep(5)     # Wait for Ben
        stt = view(X).get('status', '')
        check('F10: host line "Waiting for Ben · m:ss · press Play to keep playing with their units"', 'Waiting for Ben' in stt and 'press Play' in stt and ':0' in stt, stt)
        shot(X, 'f10_wait_line_host')
    step('f10', f10)


if __name__ == '__main__':
    import signal
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(143))
    main()
