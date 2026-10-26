#!/usr/bin/env python3
"""v13 soft restart proof (title-only Apply, no settle wait, prewarm) on test copy V (an APFS clone of copy S;
never the owner's install, prefs, saves or screen). Every window sits on a stage far off every display, no activation;
a guard kills every test process if a window with alpha > 0 is inside a display or one becomes frontmost.

  v13_test.py cold <n> <out>     A at the title -> Apply (chain A -> B -> C ...), n swaps, cold start each time
  v13_test.py prewarm <n> <out>  A at the title -> prewarm -> B ready and waiting -> Apply, n swaps
  v13_test.py world <out>        prewarm at the title, A loads a world: prewarm stopped; Apply + prewarm REFUSED in a world
  v13_test.py life <out>         same set kept; 20 s cap; set mismatch -> cold; A killed -> waiting B quits
  v13_test.py flowk <n> <out>    flow K at the title (world with other mods -> restart into it), n fresh runs
Lease: rig.py lease loaderui --pid <this pid>; released at the end (not when the lane already held that pair).
B's multiplayer control socket: env DSRIG_DIR=<V>/rigB or rigC (data/ links to <V>/data), so A keeps its own;
DSL_LOG_DIR=<V> for every process (logs + softrestart/ stay in one place).
"""
import json, os, shutil, signal, subprocess, sys, threading, time, glob
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from ctl_token import token_line as ctl_token_line   # noqa: E402  ../ctl_token.py: token first line

X = os.environ.get('V13_COPY', 'V'); I = r.idir(X)
SDIR = os.path.join(I, 'softrestart'); RIGB = os.path.join(I, 'rigB'); MODS = os.path.join(I, 'data', 'Mods')
STAGE = '-20000,-20000'
SAVE = 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0'
GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
SAVE_SEED = os.path.join(r.SEED_DATA, 'Saves', SAVE + '.json')
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py'); LANE = os.environ.get('V13_LANE', 'loaderui')
def rigdir(k):                                           # generation k of the chain: A = 0 (the copy), then rigB / rigC
    return I if k == 0 else os.path.join(I, 'rigB' if k % 2 else 'rigC')
def sock(k): return os.path.join(rigdir(k), 'ctl.sock')
def envk(k): return f'DSRIG_DIR={rigdir(k)}\n'
ENV_B = envk(1)
A_SOCK = sock(0); B_SOCK = sock(1)
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []; checks = []; server_alpha = []


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]})
    log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def game_pids():
    out = subprocess.run(['pgrep', '-f', os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def alive(pid):
    try:
        os.kill(pid, 0); return True
    except OSError:
        return False


front_trace = []                                        # front app changes (pid, command) with time


def front_watch():
    last = None
    while not stop_guard.is_set():
        fp = r.front_pid()
        if fp != last:
            cmd = subprocess.run(['ps', '-o', 'command=', '-p', str(fp or 0)], capture_output=True, text=True).stdout.strip()[-80:]
            front_trace.append((time.strftime('%H:%M:%S') + '.%03d' % (time.time() % 1 * 1000), fp, cmd)); last = fp
            if fp in game_pids():
                for p in game_pids():
                    os.kill(p, signal.SIGKILL)
                violations.append({'t': time.time(), 'front': fp, 'by': 'front_watch'}); log('FRONT WATCH KILLED', fp)
        stop_guard.wait(0.2)


def guard():
    threading.Thread(target=front_watch, daemon=True).start()
    while not stop_guard.is_set():
        pids = game_pids()
        if pids:
            ws = r.windows(pids)
            server_alpha.append([round(time.time(), 2)] + [(w['pid'], w['alpha']) for w in ws if w['w'] > 300])
            bad = [w for w in ws if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids:
                    os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


# ------------------------------------------------------------------ lease
def lease():
    while os.path.exists(os.path.expanduser('~/ds_multiplayer/rig/release.lock')):
        log('release.lock present: waiting'); time.sleep(30)
    held = subprocess.run([sys.executable, RIG, 'leases'], capture_output=True, text=True).stdout
    mine = [l.split()[0] for l in held.splitlines() if f' {LANE} ' in f' {l} ']
    for _ in range(120):
        p = subprocess.run([sys.executable, RIG, 'lease', LANE, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0:
            pair = p.stdout.strip(); log('lease', pair, '(already held by the lane: kept at the end)' if pair in mine else '')
            return pair, pair not in mine
        log('no free pair: waiting'); time.sleep(30)
    raise SystemExit('no lease')


def release(pair, own):
    if own:
        log('release', subprocess.run([sys.executable, RIG, 'release', pair, LANE], capture_output=True, text=True).stdout.strip())


# ------------------------------------------------------------------ copy V
def require_hidden_copy(app):
    import plistlib
    pl = plistlib.load(open(os.path.join(app, 'Contents', 'Info.plist'), 'rb'))
    if not (pl.get('LSBackgroundOnly') and pl.get('LSUIElement')):
        raise SystemExit(f'refused: {app} lacks LSBackgroundOnly/LSUIElement')


def prep():
    require_hidden_copy(os.path.join(I, 'DungeonSettlers.app'))
    r.prepare_data(X)                                   # muted sound settings, small window, copy's own prefs domain
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(SAVE_SEED, os.path.join(I, 'data', 'Saves', os.path.basename(SAVE_SEED)))
    for k in (1, 2):
        os.makedirs(rigdir(k), exist_ok=True)
        if not os.path.islink(os.path.join(rigdir(k), 'data')):
            os.symlink(os.path.join(I, 'data'), os.path.join(rigdir(k), 'data'))
    os.makedirs(SDIR, exist_ok=True)
    old = os.path.join(SDIR, 'old'); os.makedirs(old, exist_ok=True)
    for f in glob.glob(os.path.join(SDIR, '*')):
        if os.path.isfile(f):
            os.replace(f, os.path.join(old, os.path.basename(f) + '.%d' % time.time()))
    for s in (sock(0), sock(1), sock(2)):
        if os.path.exists(s) or os.path.islink(s):
            os.replace(s, os.path.join(old, 'sock.%d' % time.time()))


def start_a(profile, extra_env=None):
    prep()
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSL_LOG_DIR': I, 'DSRIG_FPS': '60', **(r.ctl_env() if hasattr(r, 'ctl_env') else {'DSRIG_CTL': '1'}), 'DYLD_INSERT_LIBRARIES': os.environ.get('LRIG_DYLIB') or os.path.join(I, r.DYLIB_REL),
           'DSL_NOTCH': '0',                              # the notch fix covers the screen: off in tests (as lrig.py)
           'DSL_PROFILE': profile, 'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1'}
    env.update({k: v for k, v in os.environ.items() if k.startswith('DSL_SOFT_') and k not in env})   # e.g. DSL_SOFT_BOOTLOG=1
    env.update(extra_env or {})
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('A started pid', p.pid, os.path.basename(profile))
    return p.pid


def send(sock, obj, timeout=10):
    import socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(timeout)
    s.connect(os.path.realpath(sock)); s.sendall(ctl_token_line(os.path.dirname(sock)) + (json.dumps(obj) + '\n').encode())
    buf = b''
    while not buf.endswith(b'\n'):
        k = s.recv(1 << 20)
        if not k:
            break
        buf += k
    s.close(); return json.loads(buf.decode())


def wait_status(sock, pred, what, limit=240):
    t0 = time.time(); last = None
    while time.time() - t0 < limit:
        try:
            last = send(sock, {'cmd': 'status'}, 5)
            if pred(last):
                return last
        except Exception:
            pass
        time.sleep(0.2)
    raise RuntimeError(f'timeout waiting for {what}: {last}')


def logs():
    t = ''
    for f in ('dsload.prev.log', 'dsload.log'):
        try:
            t += open(os.path.join(I, f), errors='replace').read()
        except OSError:
            pass
    return t


def blog():                                             # the newest process's own log (B after a swap / a waiting B)
    try:
        return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError:
        return ''


def wait_log(s, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in logs():
            return True
        time.sleep(0.2)
    return False


def keep_logs(out, tag):
    d = os.path.join(out, tag); os.makedirs(d, exist_ok=True)
    for f in ('dsload.log', 'dsload.prev.log'):
        if os.path.exists(os.path.join(I, f)):
            shutil.copy2(os.path.join(I, f), os.path.join(d, f))
    for f in glob.glob(os.path.join(SDIR, '*')):
        if os.path.isfile(f):
            shutil.copy2(f, os.path.join(d, os.path.basename(f)))


def request(name, obj):
    tmp = os.path.join(SDIR, name + '.tmp')
    open(tmp, 'w').write(json.dumps(obj)); os.replace(tmp, os.path.join(SDIR, name))


def clear_swap_flags():
    for x in ('go', 'shown', 'failed'):
        f = os.path.join(SDIR, 'handoff.json.' + x)
        if os.path.exists(f):
            os.replace(f, f + '.old')


def b_shown_time(pid):
    """B's own line: 'B: SHOWN (x s after apply in A' (apply = dsl_soft_apply in A, on the game clock)"""
    last = None
    for l in blog().splitlines():
        if 'B: SHOWN (' in l:
            last = float(l.split('B: SHOWN (')[1].split(' s ')[0])
    return last


def swap(a_pid, t_req, what, limit=150):
    shown = os.path.join(SDIR, 'handoff.json.shown')
    while time.time() - t_req < limit:
        if os.path.exists(shown):
            break
        if os.path.exists(os.path.join(SDIR, 'handoff.json.failed')):
            raise RuntimeError('A reported failure: ' + open(os.path.join(SDIR, 'handoff.json.failed')).read())
        time.sleep(0.02)
    else:
        raise RuntimeError(f'{what}: no swap within {limit} s')
    dt = time.time() - t_req
    while alive(a_pid) and time.time() - t_req < dt + 30:
        time.sleep(0.1)
    time.sleep(0.5)
    b = [p for p in game_pids() if p != a_pid]
    g = b_shown_time(b[0] if b else 0)
    log(f'{what}: B on screen {dt:.2f} s after the request (in game: apply -> shown {g} s); A exited {not alive(a_pid)}; B {b}')
    return dt, g, (b[0] if b else None), not alive(a_pid)


def ready_pid():
    try:
        return int(open(os.path.join(SDIR, 'handoff.json.ready')).read().strip() or 0)
    except (OSError, ValueError):
        return 0


def prewarm(profile, a_pid, k=1, limit=120):
    clear_swap_flags()
    t0 = time.time(); request('prewarm_request.json', {'profile': profile, 'env': envk(k)})
    while time.time() - t0 < limit:
        rp = ready_pid()
        if rp and rp != a_pid and alive(rp):
            dt = time.time() - t0; log(f'prewarm: B pid {rp} ready and waiting {dt:.2f} s after the request'); return rp, dt
        time.sleep(0.1)
    raise RuntimeError('prewarm B not ready')


def b_checks(pid, tag):
    """the waiting B: hidden (window server alpha), muted, idle frame cap, A still answers"""
    L = blog()
    check(f'{tag}: B muted while hidden', 'B: audio paused while hidden' in L and 'mute while hidden: volume ok, pause ok' in L,
          [l for l in L.splitlines() if 'audio' in l or 'mute' in l][-4:])
    check(f'{tag}: B idle frame cap while waiting', 'frame cap -> 10 while hidden' in L, [l for l in L.splitlines() if 'frame cap' in l][-3:])
    ws = r.windows([pid])
    check(f'{tag}: B windows alpha 0 (window server)', ws and all(w['alpha'] == 0 for w in ws), ws)
    cpu = subprocess.run(['ps', '-o', '%cpu=,rss=', '-p', str(pid)], capture_output=True, text=True).stdout.split()
    return {'cpu_pct': float(cpu[0]) if cpu else None, 'rss_mb': round(int(cpu[1]) / 1024) if len(cpu) > 1 else None}


def finish(out, summary):
    stop_guard.set()
    for p in game_pids():
        for s in (sock(1), sock(2), sock(0)):
            try:
                send(s, {'cmd': 'quit'}, 3)
            except Exception:
                pass
        time.sleep(3)
        if alive(p):
            os.kill(p, signal.SIGKILL)
    keep_logs(out, 'final')
    summary['violations'] = violations; summary['checks'] = checks
    summary['server_alpha_samples'] = len(server_alpha)
    summary['front_trace'] = front_trace
    json.dump(summary, open(os.path.join(out, 'summary.json'), 'w'), indent=1, default=str)
    n_ok = sum(c['ok'] for c in checks)
    log(f'{n_ok}/{len(checks)} checks, violations {len(violations)}, game copies left {game_pids()}, summary {out}/summary.json')


P1 = os.path.join(I, 'profile1.json'); P2 = os.path.join(I, 'profile2.json')


def run_chain(mode, n, out, summary):
    a = start_a(P1)
    wait_status(A_SOCK, lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
    time.sleep(2)
    runs = []
    for i in range(n):
        prof = P2 if i % 2 == 0 else P1
        info = {}; pw = None
        if mode == 'prewarm':
            pw, dtp = prewarm(prof, a, i + 1)
            time.sleep(3)
            info.update(b_checks(pw, f'run {i + 1}')); info['prewarm_ready_s'] = round(dtp, 2)
            try:
                st = send(sock(i), {'cmd': 'status'}, 5)
                check(f'run {i + 1}: A still answers at the title while B waits', st.get('scene') == 1 and st.get('pid', a) == a, st)
            except Exception as e:
                check(f'run {i + 1}: A still answers at the title while B waits', False, e)
        clear_swap_flags()
        t_req = time.time(); request('apply_request.json', {'profile': prof, 'env': envk(i + 1)})
        dt, g, b, a_gone = swap(a, t_req, f'{mode} run {i + 1}')
        if mode == 'prewarm':
            check(f'run {i + 1}: the prewarmed B was used', b == pw and 'the prewarmed B pid' in logs(), (b, pw))
        st = wait_status(sock(i + 1), lambda s: s.get('scene') == 1, 'B title answer', 30)
        L = blog()
        nm = (2 if prof == P2 else 1) + ('runs whenever installed' in L)   # + hell-difficulty: loaded whenever installed (0.1.28, NOTES "Hell always loaded")
        check(f'run {i + 1}: B runs {nm} module(s)', f'modules: {nm} loaded of {nm} opened' in L, [l for l in L.splitlines() if 'modules:' in l][-2:])
        check(f'run {i + 1}: A exited', a_gone)
        info.update({'request_to_shown_s': round(dt, 3), 'apply_to_shown_s': g, 'b_pid': b})
        runs.append(info); keep_logs(out, f'run{i + 1}')
        a = b
        time.sleep(2)
    summary['runs'] = runs


def run_world(out, summary):
    a = start_a(P1)
    wait_status(A_SOCK, lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
    time.sleep(2)
    b, _ = prewarm(P2, a)
    send(A_SOCK, {'cmd': 'load_save', 'name': SAVE}, 20)
    wait_status(A_SOCK, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'A in the world', 240)
    time.sleep(2)
    check('prewarm stopped when A entered a world', 'stopped: A entered a world' in logs() and not alive(b), [l for l in logs().splitlines() if 'prewarm' in l][-3:])
    pids0 = game_pids()
    clear_swap_flags(); request('apply_request.json', {'profile': P2, 'env': ENV_B}); time.sleep(2)
    L = logs()
    check('Apply in a world REFUSED', 'soft: apply refused: mods change only at the title (scene 2)' in L and 'soft test: apply' in L and '-> -1' in L.split('soft test: apply')[-1][:300],
          [l for l in L.splitlines() if 'refused' in l][-3:])
    request('prewarm_request.json', {'profile': P2, 'env': ENV_B}); time.sleep(2)
    L = logs()
    check('prewarm in a world REFUSED', 'soft: prewarm refused: mods change only at the title (scene 2)' in L, [l for l in L.splitlines() if 'refused' in l][-3:])
    time.sleep(2)
    check('no new game process, no swap', game_pids() == pids0 and not os.path.exists(os.path.join(SDIR, 'handoff.json.shown')), (pids0, game_pids()))
    st = send(A_SOCK, {'cmd': 'status'}, 5)
    check('A still in its world, running', st.get('scene') == 2 and not st.get('busy'), st)


def run_life(out, summary):
    a = start_a(P1, {'DSL_SOFT_PREWARM_MAX': '20'})
    wait_status(A_SOCK, lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
    time.sleep(2)
    t0 = time.time(); b, _ = prewarm(P2, a)
    request('prewarm_request.json', {'profile': P2, 'env': ENV_B}); time.sleep(1.5)
    check('same set again: the waiting B is kept', 'same set as the waiting B' in logs() and alive(b) and ready_pid() == b)
    while alive(b) and time.time() - t0 < 40:
        time.sleep(0.5)
    check('cap: the waiting B is stopped after DSL_SOFT_PREWARM_MAX (20 s)', not alive(b) and 'stopped: waited the maximum time' in logs(), round(time.time() - t0, 1))
    summary['cap_stop_after_s'] = round(time.time() - t0, 1)
    b, _ = prewarm(P2, a)
    clear_swap_flags(); t_req = time.time(); request('apply_request.json', {'profile': P1, 'env': ENV_B})
    dt, g, c, gone = swap(a, t_req, 'mismatch')
    L = logs()
    check('set mismatch: prewarm stopped, cold start used', not alive(b) and c != b, (b, c))
    summary['mismatch'] = {'request_to_shown_s': round(dt, 3), 'apply_to_shown_s': g}
    wait_status(sock(1), lambda s: s.get('scene') == 1, 'C title', 30); time.sleep(2)
    keep_logs(out, 'life1')
    d, _ = prewarm(P2, c, 2)
    os.kill(c, signal.SIGKILL); t_k = time.time()
    while alive(d) and time.time() - t_k < 15:
        time.sleep(0.1)
    check('A killed: the waiting B quits by itself', not alive(d), round(time.time() - t_k, 2))
    summary['orphan_quit_s'] = round(time.time() - t_k, 2)


def run_flowk(n, out, summary):
    res = []
    for i in range(n):
        os.makedirs(MODS, exist_ok=True)
        for f in glob.glob(os.path.join(MODS, '*.json')):
            os.makedirs(os.path.join(MODS, 'old'), exist_ok=True); os.replace(f, os.path.join(MODS, 'old', os.path.basename(f) + '.%d' % time.time()))
        json.dump({'enabled': ['multiplayer'], 'order': ['multiplayer']}, open(os.path.join(MODS, 'active.json'), 'w'))
        json.dump({'worlds': {GUID: 'gcd-cdr+multiplayer'}}, open(os.path.join(MODS, 'worlds.json'), 'w'))
        a = start_a(os.path.join(MODS, 'active.json'), {'DSML_SESSION': os.path.join(MODS, 'worlds.json'), 'DSML_DATA': os.path.join(I, 'data')})
        wait_status(A_SOCK, lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
        time.sleep(2)
        send(A_SOCK, {'cmd': 'load_save', 'name': SAVE}, 20)
        check(f'flow K {i + 1}: world check refused', wait_log('world check: REFUSED', 30))
        time.sleep(1.5)
        clear_swap_flags()
        t_req = time.time(); request('reg_request.json', {'cmd': 'k_ok'})
        dt, g, b, gone = swap(a, t_req, f'flow K {i + 1}')
        st = wait_status(A_SOCK, lambda s: s.get('scene') == 2 and not s.get('busy'), 'B in the world', 60)
        L = logs()
        act = json.load(open(os.path.join(MODS, 'active.json')))
        ok = 'reg: running gameplay set gcd-cdr ' in L and 'uses this mod set: allowed' in L and 'gcd-cdr' in act.get('enabled', []) and gone
        check(f'flow K {i + 1}: B in the world with gcd-cdr + multiplayer, pending -> active.json, A exited', ok, (act, gone))
        res.append({'request_to_shown_s': round(dt, 3), 'apply_to_shown_s': g, 'b_status': {k: st.get(k) for k in ('scene', 'map', 'speed', 'busy')}})
        keep_logs(out, f'flowk{i + 1}')
        for p in game_pids():
            try:
                send(A_SOCK, {'cmd': 'quit'}, 3)
            except Exception:
                pass
            time.sleep(3)
            if alive(p):
                os.kill(p, signal.SIGKILL)
        time.sleep(1)
    summary['flowk'] = res


def main():
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(SystemExit('SIGTERM')))   # timeout: still clean up (finally)
    mode = sys.argv[1]
    n = int(sys.argv[2]) if mode in ('cold', 'prewarm', 'flowk') else 0
    out = os.path.expanduser(sys.argv[-1]); os.makedirs(out, exist_ok=True)
    if game_pids():
        raise SystemExit(f'copy V already running: {game_pids()}')
    pair, own = lease()
    g = threading.Thread(target=guard, daemon=True); g.start()
    summary = {'mode': mode}
    try:
        if mode in ('cold', 'prewarm'):
            run_chain(mode, n, out, summary)
        elif mode == 'world':
            run_world(out, summary)
        elif mode == 'life':
            run_life(out, summary)
        elif mode == 'flowk':
            run_flowk(n, out, summary)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        finish(out, summary)
        release(pair, own)


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in checks) or violations or not checks else 0)   # any FAIL, guard violation or no check: exit 1
