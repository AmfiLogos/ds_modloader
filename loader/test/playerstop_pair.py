#!/usr/bin/env python3
"""TOP RULE for the multiplayer Players panel (kit 0.1.30, owner PLAN_v013 item 4: windows under a right-drag "all move
at the same time"; ds_multiplayer 51f4c12: the panel's header joins the kit's top-window choice). The Players panel shows
only in a session, so: a leased pair of hidden loader copies (host PTH with multiplayer + uikit-demo, guest PTG with
multiplayer), the host's world joined by the guest. On the host, uikit-demo's big movable kit panel covers the Players
panel's header, both orders, and a right-drag from the header (uikit-demo's fake mouse, through Unity's Input as a
player's mouse) must move ONLY the window on top:
 1. kit panel on the kit layer (sorting 30900) above the Players panel (5000): the kit panel moves, the Players panel stays;
 2. kit panel's canvas set to sorting 100, under the Players panel: the Players panel moves, the kit panel stays.
Real break caught: the window below moves too (multiplayer 74b36e6: both moved).

  playerstop_pair.py [out] [--no-build]     exit 0 = PASS; last line "RESULT playerstop_pair: N PASS, M FAIL"
  env MP_PIN=<ds_multiplayer commit> for the build (default loader/mp_pin.txt); PT_HOST / PT_GUEST = the copies."""
import json, os, shutil, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE); sys.path.insert(0, HERE)
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(L, 'test_env', 'playerstop', time.strftime('%Y%m%d-%H%M%S'))))
H_, G_ = os.environ.get('PT_HOST', 'PTH'), os.environ.get('PT_GUEST', 'PTG')
os.environ['DSL_TEST_COPY'] = H_
sys.argv = [sys.argv[0], os.path.join(OUT, 'esc')] + [a for a in sys.argv[1:] if a.startswith('--')]
import esc_test as E                                        # build() (loader + uikit-demo), DEMO, the uikit-demo request files
import lrig as r
RIGDIR = os.path.expanduser('~/ds_multiplayer/rig'); sys.path.insert(1, RIGDIR)
import rig as realrig                                       # noqa: E402  (leases only)
LANE = 'nvrel-ptop'
SAVE = os.path.join(r.SEED_DATA, 'Saves', E.SAVE + '.json')
checks, violations = [], []
stop_guard = threading.Event()


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
    return ok
def c(x, **k): return r.send(x, k, timeout=60, connect_wait=5)
def st(x): return c(x, cmd='status')
def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: v = pred()
        except Exception: v = None
        if v: return v
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def aside(p):                                               # never delete: into <out>/aside/
    if os.path.lexists(p):
        d = os.path.join(OUT, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(os.path.dirname(p)) + '_' + os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))
def hdr(): return c(H_, cmd='ui_scale_probe')               # the Players panel as drawn: hdr_px = its header, px y up
def kit_rect(): return E.st().get('w', {}).get('ovA', {}).get('r', [0, 0, 0, 0])
def delta(a, b): return (b[0] - a[0], b[1] - a[1])
def still(a, b): return all(abs(b[i] - a[i]) <= 2 for i in range(4))
def moved(a, b): d = delta(a, b); return d[0] >= DX / 3 and d[1] < 0   # followed the drag (right, down; edge clamps and the panel's scale shorten it)
DX, DY = 60, -20                                            # the drag: right and down (the panel's top edge is clamped at the screen top)


def guard():                                                # the owner's screen: a shown window or focus = kill (by our PIDs)
    while not stop_guard.is_set():
        ps = [p for p in (r.read_pid(H_), r.read_pid(G_)) if p and r.alive(p)]
        if ps:
            bad = [w for w in r.windows(ps) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in ps:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in ps:
                    try: os.kill(p, 9)
                    except OSError: pass
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


def prep(x, mp, enabled):
    I = r.idir(x)
    if r.alive(r.read_pid(x)): raise SystemExit(f'copy {x} already running')
    for p in ('mp.json', 'dsload.log', 'player.log', 'rig.log', 'fp.log', os.path.join('data', 'SaveHeaderCache.json'), os.path.join('data', 'mp_ui.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        aside(os.path.join(I, p))
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    json.dump(mp, open(os.path.join(I, 'mp.json'), 'w'))
    md = os.path.join(OUT, x, 'mods'); aside(md); os.makedirs(md)
    for m in enabled: shutil.copytree(E.DEMO if m == 'uikit-demo' else os.path.join(L, 'build', 'mods', m), os.path.join(md, m))
    prof = os.path.join(OUT, x, 'profile.json'); json.dump({'enabled': enabled, 'order': enabled}, open(prof, 'w'))
    return {'DSL_MODS_DIR': md, 'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data')}


def run():
    port = r.pair_port(H_)                                  # this run's own port (rigports)
    busy = subprocess.run(['lsof', '-nP', f'-iTCP:{port}', '-sTCP:LISTEN'], capture_output=True, text=True).stdout.strip()
    if busy: raise SystemExit(f'port {port} in use: {busy[:200]}')
    env_h = prep(H_, {'role': 'host', 'port': port, 'name': 'Host'}, ['multiplayer', 'uikit-demo'])
    env_g = prep(G_, {'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest'}, ['multiplayer'])
    for d in (E.UDRV,):
        os.makedirs(d, exist_ok=True)
        for f in ('req.json', 'resp.json'): aside(os.path.join(d, f))
    src = os.path.join(OUT, 'save_in', 'pt_world.json'); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(SAVE, src)
    os.environ.update(env_h); os.environ['DSL_UIKIT_TEST'] = E.UDRV
    r.start(H_, window=True)
    r.wait_for(H_, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=900)
    log('host load', c(H_, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H_).get('det', {}).get('locked'), timeout=900)
    os.environ.pop('DSL_UIKIT_TEST', None); os.environ.update(env_g)
    r.start(G_, window=True)
    joined = None
    try: joined = wait('guest joined', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st(G_)), timeout=600)
    except RuntimeError: pass
    if not check('session: the guest joined the host\'s world', joined): return
    time.sleep(5)
    check('players: no extra player at the start', *r.rigports.players_guard(r.send, [H_, G_], 2, 'start'))
    wait('kit ready on the host', lambda: E.st().get('kit') == 1, timeout=60, every=1)
    p0 = wait('Players panel shown on the host', lambda: (lambda p: p if p.get('shown') and p.get('hdr_px') else None)(hdr()), timeout=60)
    log('Players panel', p0)
    for order, label, top in ((0, 'kit panel above the Players panel', 'kit'), (100, 'Players panel above the kit panel', 'players')):
        j = E.ui('api', op='kp', a=order); time.sleep(1.5)
        h0 = hdr()['hdr_px']; k0 = kit_rect()
        cx, cy = (h0[0] + h0[2]) / 2, (h0[1] + h0[3]) / 2
        over = k0[0] < cx < k0[2] and k0[1] < cy < k0[3]
        if not check(f'{label}: the kit panel covers the Players panel\'s header', j.get('rc') == 0 and over, (j, h0, k0)): continue
        E.ui('api', op='rdrag_xy', x=cx, y=cy, w=DX, h=DY); time.sleep(2.5)
        h1 = hdr()['hdr_px']; k1 = kit_rect()
        if top == 'kit':
            check(f'{label}: a right-drag on the header moves the kit panel only', moved(k0, k1) and still(h0, h1), dict(hdr=(h0, h1), kit=(k0, k1)))
        else:
            check(f'{label}: a right-drag on the header moves the Players panel only', moved(h0, h1) and still(k0, k1), dict(hdr=(h0, h1), kit=(k0, k1)))
        E.ui('capture', name=f'ptop_{top}')
    E.ui('api', op='ov_off')
    check('players: no extra player at the end', *r.rigports.players_guard(r.send, [H_, G_], 2, 'end'))


def main():
    if os.path.exists(os.path.join(RIGDIR, 'release.lock')) and not os.environ.get('PT_IN_RELEASE'):
        raise SystemExit('refused: rig/release.lock exists (release gate running; PT_IN_RELEASE=1 inside it)')
    if r.owner_game_running() and os.environ.get('PT_OWNER_PLAY_OK') != '1': raise SystemExit('refused: the owner\'s game runs (this test needs 2 copies)')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: E.build()
    for k in ('DSML_ROOT', 'DSML_APP', 'DSML_SESSION', 'DSML_GAME_SAVES', 'DSRIG_DIAG', 'DSL_UIKIT_TEST'): os.environ.pop(k, None)
    os.environ.update({'LRIG_DYLIB': os.path.join(L, 'build', 'dsload.dylib'), 'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1',
                       'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_CRASHREP': '0', 'DSL_UPDATE': '0', 'DSRIG_BG': '1'})
    os.environ.update(r.soft_test_env())
    t0 = time.time(); pair = realrig.lease(LANE, os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease(LANE, os.getpid())
    if not pair: raise SystemExit('no free pair lease for 30 min')
    log('lease', pair, 'out', OUT, 'mp pin', os.environ.get('MP_PIN') or open(os.path.join(L, 'mp_pin.txt')).readline().strip())
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
        run()
    except BaseException as e:
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not violations, violations[:3])
        stop_guard.set()
        for x in (G_, H_):
            try:
                if r.alive(r.read_pid(x)): r.stop(x)
            except Exception as e: log('stop', x, repr(e))
            for f in ('dsload.log', 'rig.log', 'player.log'):
                p = os.path.join(r.idir(x), f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        realrig.release(pair, LANE)
    json.dump(checks, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    bad = [k for k in checks if not k['ok']]
    log(f'RESULT playerstop_pair: {len(checks) - len(bad)} PASS, {len(bad)} FAIL')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
