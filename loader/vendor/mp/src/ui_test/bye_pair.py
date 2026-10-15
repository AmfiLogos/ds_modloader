#!/usr/bin/env python3
"""Guest told the right thing when the session ends (ux_flow_v10 #2, lan_v9), on a leased hidden pair (menu flow, TCP):
  A. the guest's own link drops (mp_cut, no BYE): "Connection to <host> lost ... waiting for you", host waits for him;
  B. after a rejoin, the host quits: the server's P_BYE -> "<host> left the game".
  bye_pair.py [--dylib PATH]"""
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
    pair = rig.lease('uxbye', os.getpid()); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda inst, **k: rig.send(inst, k, timeout=120)
    st = lambda x: c(x, cmd='status')
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
            rig.start(G, fps=30)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)

    def guest_notice():
        wait('guest on the title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st(G)), timeout=120, every=1)
        def got():
            m = c(G, cmd='mpm_status')
            t = (m.get('notice_pending') or '') + ' ' + ((m.get('notice_box') or {}).get('text') or '')
            return t if t.strip() else None
        return wait('guest notice', got, timeout=60, every=1)

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
        rig.start(H, fps=30); rig.start(G, fps=30)
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
        press(H, 'START'); time.sleep(3)
        # A. the guest's own link drops (no BYE): the host waits for him
        log('guest link cut', c(G, cmd='mp_cut'))
        t = guest_notice(); log('A notice', t)
        left = wait('host sees the guest gone', lambda: c(H, cmd='mp_left').get('players'), timeout=30, every=1)
        check('A. own connection lost: guest told "Connection to ... lost / waiting for you", not "left the game"',
              'Connection to' in t and 'waiting for you' in t and 'left the game' not in t, t)
        check('A. host is waiting for the guest (disconnect prompt data)', bool(left), str(left))
        # B. rejoin, then the host really leaves
        join_guest(port); time.sleep(3)
        log('host quits to title', c(H, cmd='mp_quit', mode='title'))
        t = guest_notice(); log('B notice', t)
        check('B. host left: guest told "<host> left the game"', 'left the game' in t and 'Connection to' not in t, t)
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
