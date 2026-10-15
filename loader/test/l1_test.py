#!/usr/bin/env python3
"""L1 loader core on test copy S (never the owner's install, saves or screen). Windows staged far off every display.

1. A at the title, profile = data/Mods/active.json (multiplayer). The world f816... is registered in worlds.json with
   gcd-cdr+multiplayer. Checks: kit + reg + rows ready, title "Mods" button, Load list rows with "Mods: ..." lines.
2. Flow K: load that save -> refused -> "This world uses: ... Load it with them?" -> Load (test request) -> soft
   restart straight into the save with gcd-cdr + multiplayer; pending.json -> active.json in B.
3. Flow J in a world: B applies multiplayer only -> same world, world re-registered with the new set.
Output: <out>/ (logs, screenshots, summary.json).
"""
import json, os, shutil, signal, subprocess, sys, threading, time, glob
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'S'; I = r.idir(X)
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/run')
os.makedirs(OUT, exist_ok=True)
SDIR = os.path.join(I, 'softrestart'); MODS = os.path.join(I, 'data', 'Mods')
STAGE = '-20000,-20000'
SAVE = 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0'
GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
SAVE_SEED = os.path.join(r.SEED_DATA, 'Saves', SAVE + '.json')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []; checks = []


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:300]}); log('PASS' if ok else 'FAIL', name, detail if not ok else '')


def game_pids():
    out = subprocess.run(['pgrep', '-f', os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():
    while not stop_guard.is_set():
        pids = game_pids()
        if pids:
            bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids:
                    os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(0.5)


def dlog():
    try:
        return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError:
        return ''


def wait_log(s, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in dlog():
            return True
        time.sleep(0.3)
    return False


def start_a():
    r.prepare_data(X)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(SAVE_SEED, os.path.join(I, 'data', 'Saves', os.path.basename(SAVE_SEED)))
    os.makedirs(MODS, exist_ok=True)
    for f in glob.glob(os.path.join(MODS, '*.json')):
        os.makedirs(os.path.join(MODS, 'old'), exist_ok=True); os.replace(f, os.path.join(MODS, 'old', os.path.basename(f) + '.%d' % time.time()))
    json.dump({'enabled': ['multiplayer'], 'order': ['multiplayer']}, open(os.path.join(MODS, 'active.json'), 'w'))
    json.dump({'worlds': {GUID: 'gcd-cdr+multiplayer'}}, open(os.path.join(MODS, 'worlds.json'), 'w'))
    if os.path.exists(r.sockpath(X)):
        os.unlink(r.sockpath(X))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(MODS, 'active.json'), 'DSML_SESSION': os.path.join(MODS, 'worlds.json'), 'DSML_DATA': os.path.join(I, 'data'),
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1'}
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('A started pid', p.pid)
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
        time.sleep(0.3)
    raise RuntimeError(f'timeout waiting for {what}: {last}')


def alive(pid):
    try:
        os.kill(pid, 0); return True
    except OSError:
        return False


def shot(name):
    p = os.path.join(OUT, name + '.png'); request({'cmd': 'shot', 'path': p})
    t0 = time.time()
    while time.time() - t0 < 5 and not os.path.exists(p):
        time.sleep(0.1)
    log('shot', name, 'ok' if os.path.exists(p) else 'MISSING')


def request(obj):
    tmp = os.path.join(SDIR, 'reg_request.json.tmp')
    open(tmp, 'w').write(json.dumps(obj)); os.replace(tmp, os.path.join(SDIR, 'reg_request.json'))


def wait_swap(a_pid, t_req, rnd):
    shown = os.path.join(SDIR, 'handoff.json.shown')
    while time.time() - t_req < 150:
        if os.path.exists(shown):
            break
        if os.path.exists(os.path.join(SDIR, 'handoff.json.failed')):
            raise RuntimeError('A reported failure: ' + open(os.path.join(SDIR, 'handoff.json.failed')).read())
        time.sleep(0.1)
    else:
        raise RuntimeError('no swap within 150 s')
    dt = time.time() - t_req
    while alive(a_pid) and time.time() - t_req < dt + 30:
        time.sleep(0.1)
    b = [p for p in game_pids() if p != a_pid]
    log(f'round {rnd}: on screen {dt:.2f} s after the request; A exited {not alive(a_pid)}; B {b}')
    d = os.path.join(OUT, f'round{rnd}'); os.makedirs(d, exist_ok=True)
    for f in ['dsload.log', 'dsload.prev.log', 'dsload_modset.json']:
        if os.path.exists(os.path.join(I, f)):
            shutil.copy2(os.path.join(I, f), os.path.join(d, f))
    for f in glob.glob(os.path.join(MODS, '*.json')):
        shutil.copy2(f, os.path.join(d, os.path.basename(f)))
    return dt, (b[0] if b else None)


def clear_flags():
    for f in glob.glob(os.path.join(SDIR, 'handoff.json*')):
        if not f.endswith('.old'):
            os.replace(f, f + '.old')


def main():
    for p in game_pids():
        raise SystemExit(f'copy S already running: {p}')
    os.makedirs(SDIR, exist_ok=True)
    g = threading.Thread(target=guard, daemon=True); g.start()
    summary = {'violations': violations, 'checks': checks}
    try:
        a = start_a()
        st = wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
        time.sleep(2)
        L = dlog()
        check('kit ready', 'ui: kit ready' in L, [l for l in L.splitlines() if 'ui:' in l][:3])
        check('reg ready', 'reg: ready' in L)
        check('rows service on', 'rows: load list service on' in L)
        check('running set multiplayer', 'reg: running gameplay set multiplayer' in L)
        check('no refused hook', 'refused' not in L.replace('world check: REFUSED', ''), [l for l in L.splitlines() if 'refused' in l][:3])
        shot('1_title')
        r.send(X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', wait_log('rows: Load list rebuilt', 20), [l for l in dlog().splitlines() if 'rows:' in l][-3:])
        time.sleep(1); shot('2_loadlist')
        L = dlog(); check('row line has world rows', any('world row(s)' in l and not ' 0 world' in l for l in L.splitlines() if 'Load list rebuilt' in l))
        # flow K
        r.send(X, {'cmd': 'load_save', 'name': SAVE}, timeout=20, connect_wait=0)
        check('world check refused', wait_log('world check: REFUSED', 30))
        time.sleep(1.5); shot('3_question')
        clear_flags()
        t_req = time.time(); request({'cmd': 'k_ok'})
        dt, b = wait_swap(a, t_req, 1)
        summary['flowK_seconds'] = round(dt, 2)
        st = wait(lambda s: s.get('scene') == 2 and not s.get('busy'), 'B in the world', 60)
        L = dlog()
        check('B runs gcd-cdr + multiplayer', 'reg: running gameplay set gcd-cdr+multiplayer' in L)
        check('B world check allowed', 'uses this mod set: allowed' in L)
        act = json.load(open(os.path.join(MODS, 'active.json')))
        check('pending -> active.json', 'gcd-cdr' in act.get('enabled', []), act)
        time.sleep(3); shot('4_in_world_B')
        # flow J in a world
        clear_flags()
        r.send(X, {'cmd': 'set_speed', 'speed': 2}); time.sleep(1.5)
        t_req = time.time(); request({'cmd': 'apply', 'ids': ['multiplayer']})
        dt2, c = wait_swap(b, t_req, 2)
        summary['flowJ_world_seconds'] = round(dt2, 2)
        st = wait(lambda s: s.get('scene') == 2 and not s.get('busy'), 'C in the world', 60)
        L = dlog()
        check('C runs multiplayer only', 'reg: running gameplay set multiplayer' in L)
        check('C world check allowed (world re-registered)', 'uses this mod set: allowed' in L)
        w = json.load(open(os.path.join(MODS, 'worlds.json')))
        check('worlds.json: world now multiplayer', w.get('worlds', {}).get(GUID) == 'multiplayer', w)
        act = json.load(open(os.path.join(MODS, 'active.json')))
        check('active.json multiplayer only', act.get('enabled') == ['multiplayer'], act)
        summary['C_status'] = {k: st.get(k) for k in ('scene', 'map', 'tick', 'speed')}
        time.sleep(2); shot('5_in_world_C')
    except Exception as e:
        check('run finished', False, e)
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
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(I, f)):
                shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        n_ok = sum(c['ok'] for c in checks)
        log(f'{n_ok}/{len(checks)} checks, violations {len(violations)}, summary {OUT}/summary.json')


if __name__ == '__main__':
    main()
