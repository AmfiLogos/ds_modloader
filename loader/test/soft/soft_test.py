#!/usr/bin/env python3
"""Soft restart proof on test copy S (never the owner's install, saves or screen).

Round 1: A at the title with profile1 (multiplayer) -> apply profile2 (multiplayer + gcd-cdr) -> B at the title.
Round 2: that B, in a loaded world, camera nudged, speed Fast -> apply profile1 -> new B in the same world.
Windows: every window of the test sits on a stage far off every display (DSL_SOFT_STAGE), no activation.
A guard kills every test process if a window with alpha > 0 is ever inside a display or one becomes frontmost.
Output: <out>/round<N>/ (dsload logs, frame logs, handoff, captures) + summary.json.
"""
import json, os, shutil, signal, subprocess, sys, threading, time, glob
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'S'
I = r.idir(X)
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/soft_out/run')
os.makedirs(OUT, exist_ok=True)
SDIR = os.path.join(I, 'softrestart')
STAGE = '-20000,-20000'
SAVE_SEED = os.path.join(r.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []


def game_pids():
    out = subprocess.run(['pgrep', '-f', os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


server_alpha = []                       # window server's view of every test window, once a second


def guard():
    while not stop_guard.is_set():
        pids = game_pids()
        if pids:
            ws = r.windows(pids)
            server_alpha.append([round(time.time(), 2)] + [(w['pid'], w['id'], w['alpha'], w['layer']) for w in ws if w['w'] > 300])
            bad = [w for w in ws if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids:
                    os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


def require_hidden_copy(app):
    """refuse a test copy that could take focus or show in the Dock (lead rule 2026-10-15)"""
    import plistlib
    pl = plistlib.load(open(os.path.join(app, 'Contents', 'Info.plist'), 'rb'))
    if not (pl.get('LSBackgroundOnly') and pl.get('LSUIElement')):
        raise SystemExit(f'refused: {app} lacks LSBackgroundOnly/LSUIElement (rebuild it with make_instance.sh)')


def start_a(profile):
    require_hidden_copy(os.path.join(I, 'DungeonSettlers.app'))
    r.prepare_data(X)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(SAVE_SEED, os.path.join(I, 'data', 'Saves', os.path.basename(SAVE_SEED)))
    if os.path.exists(r.sockpath(X)):
        os.unlink(r.sockpath(X))
    os.makedirs(os.path.join(OUT, 'cap'), exist_ok=True)
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **(r.ctl_env() if hasattr(r, 'ctl_env') else {'DSRIG_CTL': '1'}), 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(I, profile), 'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1',
           'DSL_SOFT_CAPTURE': os.path.join(OUT, 'cap'), 'DSL_NO_ALERT': '1'}
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('A started pid', p.pid, profile)
    return p.pid


def status(timeout=5):
    return r.send(X, {'cmd': 'status'}, timeout=timeout, connect_wait=0)


def wait(pred, what, limit=240):
    t0 = time.time(); last = None
    while time.time() - t0 < limit:
        try:
            last = status()
            if pred(last):
                return last
        except Exception:
            pass
        time.sleep(0.2)
    raise RuntimeError(f'timeout waiting for {what}: {last}')


def alive(pid):
    try:
        os.kill(pid, 0); return True
    except OSError:
        return False


def apply(profile, rnd, a_pid, camera_move=None):
    for f in glob.glob(os.path.join(SDIR, 'handoff.json*')):
        os.replace(f, f + '.old') if not f.endswith('.old') else None
    req = {'profile': os.path.join(I, profile)}
    if camera_move:
        req['camera_move'] = camera_move
    tmp = os.path.join(SDIR, 'apply_request.json.tmp')
    open(tmp, 'w').write(json.dumps(req)); os.replace(tmp, os.path.join(SDIR, 'apply_request.json'))
    t_req = time.time(); log(f'round {rnd}: apply {profile} requested')
    shown = os.path.join(SDIR, 'handoff.json.shown')
    while time.time() - t_req < 150:
        if os.path.exists(shown):
            break
        if os.path.exists(os.path.join(SDIR, 'handoff.json.failed')):
            raise RuntimeError('A reported failure: ' + open(os.path.join(SDIR, 'handoff.json.failed')).read())
        time.sleep(0.1)
    else:
        raise RuntimeError('no swap within 150 s')
    t_shown = time.time()
    while alive(a_pid) and time.time() - t_shown < 30:
        time.sleep(0.1)
    hand = json.load(open(os.path.join(SDIR, 'handoff.json')))
    b_pid = [p for p in game_pids() if p != a_pid]
    log(f'round {rnd}: shown {t_shown - t_req:.2f} s after the request; A alive: {alive(a_pid)}; B pids {b_pid}')
    d = os.path.join(OUT, f'round{rnd}'); os.makedirs(d, exist_ok=True)
    return {'t_request_to_shown': round(t_shown - t_req, 3), 'a_exited': not alive(a_pid), 'b_pid': b_pid[0] if b_pid else None, 'handoff': hand, 'dir': d}


def collect(res):
    d = res['dir']
    for f in ['dsload.log', 'dsload.prev.log', 'player.log', 'dsload_modset.json']:
        if os.path.exists(os.path.join(I, f)):
            shutil.copy2(os.path.join(I, f), os.path.join(d, f))
    for f in glob.glob(os.path.join(SDIR, '*')):
        if os.path.isfile(f):
            shutil.copy2(f, os.path.join(d, os.path.basename(f)))


def main():
    for p in game_pids():
        raise SystemExit(f'copy S already running: {p}')
    os.makedirs(SDIR, exist_ok=True)
    for f in glob.glob(os.path.join(SDIR, 'softrestart_frames_*.log')):
        os.makedirs(os.path.join(SDIR, 'old'), exist_ok=True); os.replace(f, os.path.join(SDIR, 'old', os.path.basename(f)))
    g = threading.Thread(target=guard, daemon=True); g.start()
    summary = {'violations': violations, 'server_alpha': server_alpha}
    try:
        a = start_a('profile1.json')
        st = wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
        log('A at the title', st.get('frames'))
        time.sleep(2)
        r1 = apply('profile2.json', 1, a)
        st = wait(lambda s: s.get('scene') == 1, 'B title answer', 30)
        r1['b_status'] = {k: st.get(k) for k in ('scene', 'busy', 'frames')}
        collect(r1); summary['round1'] = r1
        b = r1['b_pid']
        # round 2: world
        st = r.send(X, {'cmd': 'load_save', 'name': os.path.basename(SAVE_SEED)[:-5]})
        log('round 2: load_save', st)
        st = wait(lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world loaded', 240)
        time.sleep(3)
        r.send(X, {'cmd': 'set_speed', 'speed': 2}); time.sleep(1.5)
        st = status(); log('round 2: world running, speed', st.get('speed'), 'tick', st.get('tick'))
        summary['round2_before'] = {k: st.get(k) for k in ('scene', 'map', 'tick', 'speed')}
        r2 = apply('profile1.json', 2, b, camera_move=[3, 2])
        time.sleep(3)
        st = wait(lambda s: s.get('scene') == 2, 'B2 in the world', 30)
        r2['b_status'] = {k: st.get(k) for k in ('scene', 'map', 'tick', 'speed', 'busy')}
        collect(r2); summary['round2'] = r2
        log('round 2: B2 status', r2['b_status'])
    finally:
        stop_guard.set()
        for p in game_pids():
            try:
                r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception:
                pass
            time.sleep(3)
            if alive(p):
                os.kill(p, signal.SIGKILL)
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log('summary', os.path.join(OUT, 'summary.json'), 'violations', len(violations))


if __name__ == '__main__':
    main()
