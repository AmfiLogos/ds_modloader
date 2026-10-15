#!/usr/bin/env python3
"""Player colour follows the stable uid, not the slot (owner 2026-10-14: one colour per player, a rejoin keeps it).
On a leased hidden pair (menu flow, TCP):
  1. host + guest: host colour index 0 (gold), guest (slot 1) index 1 (blue), the same on both peers;
  2. the host imports a world history where a third player "Xavier" came first (index 1) and the guest has index 2
     (DSNET_OWNERS_LOAD kind 0 + kind 3, the same path a saved world uses): slot 1 now shows index 2 (orchid) on both;
  3. the guest quits to the title and rejoins by address: still index 2 on both (by slot it would be 1, blue),
     (mp_colors reads ui_player_color, the function every panel / tag / chip uses).
  color_rejoin_pair.py [--dylib PATH]"""
import json, os, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15)
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    a = sys.argv
    rig.DYLIB = a[a.index('--dylib') + 1] if '--dylib' in a else os.path.expanduser('~/ds_multiplayer/build/uxcolor/dsrig.dylib')
    pair = rig.lease('uxcolor', os.getpid()); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda inst, **k: rig.send(inst, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                r = pred()
            except Exception:
                r = None
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def join_guest(port):
        if not rig.alive(rig.read_pid(G)):
            rig.start(G, fps=30, window=True)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)

    def colors(x):
        r = c(x, cmd='mp_colors')
        return {s['slot']: (s['index'], s['color']) for s in r.get('slots', [])}, r.get('me')

    for x in (H, G):
        assert not rig.alive(rig.read_pid(x)), f'{x} is running (not mine)'
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
        d = os.path.join(rig.idir(x), 'data', 'mp_owners')
        if os.path.isdir(d):
            os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ before start'
        open(os.path.join(rig.idir(H), 'data', 'mp_ui.json'), 'w').write('{"see_through":0,"names":1,"list_open":1}\n')
        rig.start(H, fps=30, window=True); rig.start(G, fps=30, window=True)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        join_guest(port)
        press(H, 'START')
        time.sleep(3)
        (ch, _), (cg, gme) = colors(H), colors(G)
        log('session start: host view', ch, 'guest view', cg, 'guest slot', gme)
        check('1. host index 0 gold, guest slot 1 index 1 blue, same on both',
              gme == 1 and ch[0] == cg[0] == (0, '#F2C14E') and ch[1] == cg[1] == (1, '#5DADE2'), f'{ch} {cg}')

        uid_g = open(os.path.join(rig.idir(G), 'data', 'mp_uid'), 'rb').read()
        assert len(uid_g) == 16
        uid_x = bytes([0x58] * 16)
        name = b'Xavier'.ljust(32, b'\0')
        blob = bytes([0, 1, 0]) + uid_x + name + bytes([3, 2, 0]) + uid_x + bytes([1]) + uid_g + bytes([2])
        log('host imports the history', c(H, cmd='mp_import_owners', hex=blob.hex()))
        time.sleep(4)
        (ch, _), (cg, _) = colors(H), colors(G)
        check('2. after the world history: guest (slot 1) index 2 orchid on both', ch[1] == cg[1] == (2, '#E88BD4') and ch[0] == cg[0] == (0, '#F2C14E'), f'{ch} {cg}')

        r = c(G, cmd='mp_quit', mode='stock'); log('guest quits to title', r)
        if not r.get('ok'):
            c(G, cmd='mp_quit', mode='title')
        wait('guest on the title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st(G)), timeout=120, every=1)
        time.sleep(2)
        (ch, _) = colors(H)
        check('3a. while away: no colour for slot 1 on the host', ch[1][0] == -1, str(ch))
        join_guest(port)
        time.sleep(4)
        (ch, _), (cg, gme) = colors(H), colors(G)
        log('after rejoin: host view', ch, 'guest view', cg, 'guest slot', gme)
        check('3. rejoined: guest keeps index 2 orchid on both (by slot it would be blue)',
              ch[gme] == cg[gme] == (2, '#E88BD4'), f'slot {gme}: {ch} {cg}')
    finally:
        for x in (H, G):
            try:
                rig.stop(x)
            except Exception as e:
                log('stop', x, e)
        rig.release(pair)
        log('check-prefs', rig.check_prefs())
        log('RESULTS', json.dumps(results))


if __name__ == '__main__':
    main()
