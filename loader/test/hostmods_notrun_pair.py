#!/usr/bin/env python3
"""A guest whose gameplay mod is on but does not start (crash guard) is refused once, with the reason, and is never
restarted (lane dsmpfix 2026-10-25, lead's pick). A leased pair of hidden loader copies (never the owner's game, saves or
screen); the host listens on an OS-chosen port (never a shared fixed one).

Scenario: host and guest both have X = gcd-cdr installed and on (the same files). The guest's start finds
dsload_state/gcd-cdr.pending (what a crash during X's start leaves), so its crash guard turns X off: X is not in the
guest's multiplayer set. The guest joins by address (title > Multiplayer > Join by address > Connect) with
DSMP_HOSTMODS_TEST=1 (the test hook clicks "Get the host's mods" 2 s after the box shows, as a player would).
The break it catches: the box said "you have it, turned off" with the button; the click soft-restarted the game with the
same set, the crash guard kept X off, the new process rejoined and was refused again (again on every click).
Checks: host runs X; guest: X off by the crash guard and not in its set; refused (MODSET) and the box says "<X name> is on
but did not start on this computer: <reason>" with no button; for --wait seconds (default 45): no "hostmods: get:", the
same guest process, one box, one join (no rejoin), the guest stays on the title.

  hostmods_notrun_pair.py [--wait 45]
Inputs: HM_BUILD = loader build folder (dsload.dylib + mods/multiplayer + mods/gcd-cdr; default ../build), HM_SRC_APP =
a ready hidden test copy to clone (default ~/ds_launcher/loader/test_env/instances/Z). Copies NRH (host) and NRG (guest) in
../test_env/instances. Lease: ~/ds_multiplayer/rig/rig.py (refused while rig/release.lock exists).
Output: ../test_env/notrun/<time>/ (logs, results.json); RESULT PASS|FAIL line."""
import json, os, signal, shutil, socket, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lrig  # noqa: E402
RIGDIR = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(1, RIGDIR)
import rig as realrig  # noqa: E402  (leases only; its copies are never touched)

lrig.INST = os.path.join(L, 'test_env', 'instances')
H, G = 'NRH', 'NRG'
X_ID = 'gcd-cdr'
STAGE = '-20000,-20000'
SAVE = os.path.join(lrig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
B = dict(MULTIPLAYER=1, JOIN_ADDR=14, CONNECT=15)                  # src/mpmenu.h enum mpm_button
WAIT = int(sys.argv[sys.argv.index('--wait') + 1]) if '--wait' in sys.argv else 45
BUILD = os.environ.get('HM_BUILD', os.path.join(L, 'build'))
OUT = os.path.join(L, 'test_env', 'notrun', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
LANE = 'dsmpfix-notrun'
checks, info = [], {}
stop_guard = threading.Event(); violations = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
    return ok
def c(x, /, **k): return lrig.send(x, k, timeout=120)
def st(x):
    try: return c(x, cmd='status')
    except Exception as e: return {'error': repr(e)}
def wait(what, pred, timeout=600, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: r = pred()
        except (OSError, ValueError, RuntimeError): r = None
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def park(p):                                                     # never delete: into the copy's old/ folder
    if os.path.lexists(p):
        d = os.path.join(os.path.dirname(p), 'old'); os.makedirs(d, exist_ok=True)
        os.replace(p, os.path.join(d, f'{os.path.basename(p)}.{time.time():.0f}'))
def exe(x): return os.path.join(lrig.idir(x), lrig.EXE_REL)
def pids(x): return [int(p) for p in subprocess.run(['pgrep', '-f', exe(x)], capture_output=True, text=True).stdout.split()]
def flog(x, name, pos=0):
    try:
        with open(os.path.join(lrig.idir(x), name), 'rb') as f: f.seek(pos); return f.read().decode('utf8', 'replace')
    except OSError: return ''
def fsize(x, name):
    p = os.path.join(lrig.idir(x), name); return os.path.getsize(p) if os.path.exists(p) else 0
def session_lines(x):
    try: return json.load(open(os.path.join(lrig.idir(x), 'data', 'Mods', 'worlds.json'))).get('modset_lines')
    except Exception: return None


def guard():                                                     # the owner's screen: a shown window or focus = kill
    while not stop_guard.is_set():
        ps = pids(H) + pids(G)
        if ps:
            bad = [w for w in lrig.windows(ps) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = lrig.front_pid()
            if bad or fp in ps:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in ps:
                    try: os.kill(p, signal.SIGKILL)
                    except OSError: pass
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


def make_copy(x):
    app = os.path.join(lrig.idir(x), 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.environ.get('HM_SRC_APP', os.path.expanduser('~/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app'))
        if not os.path.exists(os.path.join(src, lrig.EXE_REL.split('/', 1)[1])):
            raise SystemExit(f'no ready test copy to clone at {src} (make_instance.sh first)')
        os.makedirs(lrig.idir(x), exist_ok=True)
        subprocess.run(['cp', '-c', '-R', src, app], check=True)
        pl = os.path.join(app, 'Contents', 'Info.plist')
        for k, t, v in (('CFBundleIdentifier', '-string', f'com.CanOpener.Dungeon-Settlers.mp{x}'), ('CFBundleName', '-string', f'DS rig {x}'),
                        ('LSBackgroundOnly', '-bool', 'YES'), ('LSUIElement', '-bool', 'YES')):
            subprocess.run(['plutil', '-replace', k, t, v, pl], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
        log(x, 'copy made from', src)
    lrig.require_background_only(x)
    for d in ('home', 'data/Saves', 'data/Mods'):
        os.makedirs(os.path.join(lrig.idir(x), d), exist_ok=True)


def kit(x, crashed):
    """the player's layout: <copy>/kit/loader/{dsload.dylib, RELEASE, mods/<id>}; X on in the profile"""
    I = lrig.idir(x); k = os.path.join(I, 'kit'); park(k)
    md = os.path.join(k, 'loader', 'mods'); os.makedirs(md); os.makedirs(os.path.join(k, 'mods')); os.makedirs(os.path.join(k, 'update'))
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(k, 'loader', 'dsload.dylib'))
    shutil.copy2(os.path.join(os.path.dirname(L), 'RELEASE'), os.path.join(k, 'loader', 'RELEASE'))
    for i in ('multiplayer', X_ID): shutil.copytree(os.path.join(BUILD, 'mods', i), os.path.join(md, i))
    mods = os.path.join(I, 'data', 'Mods')
    for f in os.listdir(mods):
        if f.endswith('.json'): park(os.path.join(mods, f))
    json.dump({'enabled': ['multiplayer', X_ID], 'order': ['multiplayer', X_ID]}, open(os.path.join(mods, 'active.json'), 'w'))
    for f in ('rig.log', 'dsload.log', 'dsload.prev.log', 'fp.log', 'player.log', 'mp.json', 'softrestart', 'dsload_state'):
        park(os.path.join(I, f))
    if crashed:                                                   # what a crash during X's start leaves (dsl_main.c guard)
        os.makedirs(os.path.join(I, 'dsload_state'))
        open(os.path.join(I, 'dsload_state', X_ID + '.pending'), 'w').write('dsl_plugin_init (test: a crash during the start)')


def start(x, extra=None):
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    lrig.require_background_only(x); lrig.prepare_data(x); lrig.require_windowed(x)
    if os.path.exists(lrig.sockpath(x)): os.unlink(lrig.sockpath(x))   # our copy's own socket file
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '30', **lrig.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(k, 'loader', 'dsload.dylib'),
           'DSL_MODS_DIR': os.path.join(k, 'loader', 'mods'), 'DSML_ROOT': k, 'DSML_USER_MODS_DIR': os.path.join(k, 'mods'),
           'DSL_PROFILE': os.path.join(I, 'data', 'Mods', 'active.json'), 'DSML_SESSION': os.path.join(I, 'data', 'Mods', 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSL_UPDATE': '0', 'DSL_CLOUD': '0', 'DSL_CRASHREP': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSMP_TEST': '1'}
    env.update(lrig.soft_test_env()); env.update(extra or {})
    args = [exe(x), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(lrig.pidfile(x), 'w').write(f'{p.pid}\n'); log(x, 'started pid', p.pid)
    return p.pid


def stop(x):
    for p in pids(x):
        try: lrig.send(x, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        for _ in range(50):
            if p not in pids(x): break
            time.sleep(0.1)
        if p in pids(x): os.kill(p, signal.SIGKILL); log(x, 'killed pid', p)


def run(port):
    x_name = json.load(open(os.path.join(BUILD, 'mods', X_ID, 'mod.json'), encoding='utf-8'))['name']
    kit(H, False); kit(G, True)
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(lrig.idir(H), 'mp.json'), 'w'))   # the guest has none: it joins through the menu
    start(H)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    if 'cannot listen' in flog(H, 'rig.log'): raise RuntimeError(f'the host cannot listen on port {port}')
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'notrun_base.json'); shutil.copy2(SAVE, src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H).get('det', {}).get('locked'), timeout=900, every=2)
    hl = session_lines(H)
    check('host runs X: X in its multiplayer set', hl and any(l.startswith(X_ID + '@') for l in hl), hl)

    a_pid = start(G, {'DSMP_HOSTMODS_TEST': '1'})
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3)
    gl = session_lines(G); dl = flog(G, 'dsload.log')
    check('guest: X is on but the crash guard turned it off; X not in its multiplayer set',
          f'module {X_ID} is OFF by the crash guard' in dl and gl is not None and not any(l.startswith(X_ID + '@') for l in gl),
          f'lines {gl}; guard lines {[l for l in dl.splitlines() if "CRASH" in l or "crash guard" in l][:3]}')
    rpos, dpos = fsize(G, 'rig.log'), fsize(G, 'dsload.log')
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=B['CONNECT'])
    log('guest: join by address sent')
    box = wait('hostmods box', lambda: next((l for l in flog(G, 'rig.log', rpos).splitlines() if 'mp: hostmods box:' in l), None), timeout=180, every=0.2)
    info['box_line'] = box
    try: info['box_shot'] = c(G, cmd='mpm_shot', path=os.path.join(OUT, 'notrun_box.png'), how='backbuffer')
    except Exception as e: info['box_shot'] = repr(e)
    gtxt = flog(G, 'rig.log', rpos)
    refusal = [l for l in gtxt.splitlines() if 'modset refusal' in l or 'REJ_MODSET' in l]
    check('guest refused (MODSET), the box shows', bool(refusal) and bool(box), f'refusal {refusal[-1:]}; box {box}')
    want = f'{x_name} is on but did not start on this computer: it stopped the game while starting last time'
    check('the box says the mod is on but did not start here, with the reason; no "Get the host\'s mods" button',
          want in box and '[button none]' in box and 'turned off' not in box, box)

    time.sleep(WAIT)                                              # the test hook would have clicked at 2 s
    gtxt, dl = flog(G, 'rig.log', rpos), flog(G, 'dsload.log', dpos)
    gets = [l for l in (dl + flog(G, 'dsload.prev.log')).splitlines() if 'hostmods: get:' in l]
    boxes = [l for l in gtxt.splitlines() if 'mp: hostmods box:' in l]
    joins = [l for l in gtxt.splitlines() if 'join by address' in l and ' rc ' in l]
    s = st(G)
    check(f'no restart, no rejoin loop ({WAIT} s): no get, the same guest process, one box, one join, still on the title',
          not gets and pids(G) == [a_pid] and len(boxes) == 1 and len(joins) == 1 and s.get('scene') == 1,
          f'gets {gets[:2]}; pids {pids(G)} (first {a_pid}); boxes {len(boxes)}; joins {joins[:3]}; scene {s.get("scene")}')


def main():
    if os.path.exists(os.path.join(RIGDIR, 'release.lock')): raise SystemExit('refused: rig/release.lock exists (release gate running)')
    for x in (H, G):
        if pids(x): raise SystemExit(f'copy {x} already running: {pids(x)}')
    if not os.path.exists(os.path.join(BUILD, 'dsload.dylib')): raise SystemExit(f'no loader build in {BUILD}')
    t0 = time.time(); pair = realrig.lease(LANE, os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease(LANE, os.getpid())
    if not pair: raise SystemExit('no free pair lease for 30 min')
    s = socket.socket(); s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]; s.close()   # OS-chosen: never a shared port
    log('lease', pair, 'port', port, 'build', BUILD, 'out', OUT)
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
        for x in (H, G): make_copy(x)
        run(port)
    except BaseException as e:
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not violations, violations[:3])
        stop_guard.set()
        for x in (G, H):
            try: stop(x)
            except Exception as e: log('stop', x, repr(e))
        for x in (G, H):
            for f in ('dsload.log', 'dsload.prev.log', 'rig.log', 'player.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        realrig.release(pair, LANE)
        res = {'checks': checks, 'all_pass': all(k['ok'] for k in checks) and bool(checks), 'info': info}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        n = sum(k['ok'] for k in checks)
        log(f'RESULT {"PASS" if res["all_pass"] else "FAIL"} {n} PASS, {len(checks) - n} FAIL; {OUT}')
    sys.exit(0 if res['all_pass'] else 1)


if __name__ == '__main__':
    main()
