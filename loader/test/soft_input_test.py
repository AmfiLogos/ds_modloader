#!/usr/bin/env python3
"""Player report 2026-10-20 (Windows): while mods were being applied (soft restart, the game's loading screen up and
the new game starting), the title under the loading screen still showed tooltips and took clicks. The game's own
loads hide the title at once; only the soft restart keeps it under the loading screen (10+ s on Windows).

On a hidden Mac test copy (default N, DSL_TEST_COPY) with the loader + test module title-input-probe only:
 1. Apply, the new game (B) is killed at once -> the restart fails, the title comes back: a click on Settings must work
    (input given back).
 2. Apply again, then fake clicks on the stock Settings button every 0.25 s until the old game quits: none may reach
    TitleMenuView.OnSettingsPressed. Old loaders fail here (1-2 clicks get through in 2.7 s on Mac).
  soft_input_test.py [out] [--no-build]          Exit 0 = PASS. Output: <out>/summary.json, dsload logs."""
import glob, json, os, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); LW = os.path.dirname(HERE); sys.path.insert(0, HERE)
import lrig as r
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(LW, 'test_env', 'l1_out', 'soft_input')))
X = os.environ.get('DSL_TEST_COPY', 'N'); I = r.idir(X); APP = os.path.join(I, 'DungeonSettlers.app')
DRV = os.path.join(OUT, 'drv'); PROBE = os.path.join(LW, 'build', 'test_mods', 'title-input-probe')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []; violations = []; stop = threading.Event()


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:300]}); log('PASS' if ok else 'FAIL', name, detail)


def game_pids():
    out = subprocess.run(['pgrep', '-f', '^' + os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():
    while not stop.is_set():
        pids = game_pids()
        if pids:
            bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids: os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop.wait(0.5)


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)


def build():
    os.makedirs(PROBE, exist_ok=True)
    src = os.path.join(HERE, 'mods_src', 'title-input-probe')
    subprocess.run(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation '
                   f'-install_name @rpath/title-input-probe.dylib -o "{PROBE}/title-input-probe.dylib.tmp" "{src}/title_input_probe.c" '
                   f'&& codesign -f -s - "{PROBE}/title-input-probe.dylib.tmp" 2>/dev/null; mv -f "{PROBE}/title-input-probe.dylib.tmp" "{PROBE}/title-input-probe.dylib"',
                   shell=True, check=True)
    shutil.copy2(os.path.join(src, 'mod.json'), PROBE)


def install():
    if game_pids(): raise SystemExit(f'copy {X} is running: not installing over it')
    put(os.path.join(LW, 'build', 'dsload.dylib'), os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'))
    for f in os.listdir(PROBE): put(os.path.join(PROBE, f), os.path.join(APP, 'Contents', 'Resources', 'mods', 'title-input-probe', f))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def start():
    r.require_background_only(X); r.prepare_data(X); r.require_windowed(X)
    # without the multiplayer module, persistentDataPath is the copy's own home folder: the same windowed settings there
    pd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    assert json.load(open(os.path.join(pd, 'UserSetting.json')))['GraphicSettingData']['SaveCurrentScreenType'] == 1
    prof = os.path.join(I, 'soft_input_profile.json'); json.dump({'enabled': ['title-input-probe'], 'order': ['title-input-probe']}, open(prof, 'w'))
    os.makedirs(DRV, exist_ok=True)
    for f in ('req.json', 'resp.json'):
        if os.path.exists(os.path.join(DRV, f)): os.replace(os.path.join(DRV, f), os.path.join(DRV, f + '.old'))
    crd = os.path.join(OUT, 'crashrep'); os.makedirs(crd, exist_ok=True); open(os.path.join(crd, 'answer'), 'w').write('no')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'),
           'DSL_PROFILE': prof, 'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
           'DSL_NOTCH': '0', 'DSL_UPDATE': '0', 'DSL_CLOUD': '0', 'DSL_PREWARM': '0', 'DSL_CRASHREP_TEST': crd, 'DSL_TITLEPROBE': DRV}
    env.update(r.soft_test_env())
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('started', p.pid)
    return p.pid


SEQ = [0]
def cmd(c, timeout=15, **kw):
    SEQ[0] += 1; kw.update({'cmd': c, 'seq': SEQ[0]})
    tmp = os.path.join(DRV, 'req.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(DRV, 'req.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            j = json.load(open(os.path.join(DRV, 'resp.json')))
            if j.get('seq') == SEQ[0]: return j
        except Exception: pass
        time.sleep(0.05)
    return {'timeout': c}


def settings_presses(timeout=15):
    return cmd('state', timeout=timeout).get('presses', [-1])[0]


def apply():
    prof = os.path.join(I, 'soft_input_empty.json'); json.dump({'enabled': [], 'order': []}, open(prof, 'w'))
    sd = os.path.join(I, 'softrestart'); os.makedirs(sd, exist_ok=True)
    open(os.path.join(sd, 'apply_request.json.tmp'), 'w').write(json.dumps({'profile': prof}))
    os.replace(os.path.join(sd, 'apply_request.json.tmp'), os.path.join(sd, 'apply_request.json'))


def kill_all():
    for p in game_pids():
        try: os.kill(p, signal.SIGTERM)
        except OSError: pass
    time.sleep(4)
    for p in game_pids():
        try: os.kill(p, signal.SIGKILL)
        except OSError: pass


def main():
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: build()
    install()
    threading.Thread(target=guard, daemon=True).start()
    a_pid = start()
    try:
        t0 = time.time()
        while time.time() - t0 < 240:                                   # the title fades in a few s after it is ready
            if cmd('click', target='Settings', timeout=3).get('rc', -1) >= 0:
                time.sleep(1.5)
                if settings_presses(3) >= 1: break
            time.sleep(1)
        check('title up, Settings click works before any Apply', settings_presses() >= 1)
        # 1. failed restart: the input comes back
        p0 = settings_presses(); apply(); t0 = time.time(); killed = None
        while time.time() - t0 < 20 and not killed:
            b = [p for p in game_pids() if p != a_pid]
            if b: os.kill(b[0], signal.SIGKILL); killed = b[0]
            time.sleep(0.1)
        t1 = time.time()
        while time.time() - t1 < 30 and cmd('state', timeout=3).get('blocked') != 0: time.sleep(0.5)
        cmd('click', target='Settings', timeout=20); time.sleep(2)
        p1 = settings_presses(20)
        check('failed restart: title input given back (Settings click works)', killed and p1 > p0, f'B {killed}, presses {p0} -> {p1}')
        # 2. restart that completes: clicks under the loading screen
        p2 = settings_presses(); apply(); t0 = time.time()
        while time.time() - t0 < 10 and cmd('state').get('blocked') not in (1, -1): time.sleep(0.05)
        time.sleep(0.1); n = 0; last = p2
        while time.time() - t0 < 60:
            j = cmd('click', target='Settings', timeout=3)
            if 'timeout' in j: break
            n += 1; last = j.get('presses', [last])[0]; time.sleep(0.25)
        check('restart: no click reaches the title under the loading screen', n >= 3 and last == p2, f'{n} clicks, presses {p2} -> {last}')
    finally:
        for f in glob.glob(os.path.join(I, 'dsload*.log*')) + [os.path.join(I, 'player.log')]:
            try: shutil.copy2(f, OUT)
            except OSError: pass
        kill_all(); stop.set()
    ok = all(c['ok'] for c in checks) and not violations and len(checks) == 3
    json.dump({'checks': checks, 'violations': violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    print('RESULT', 'PASS' if ok else 'FAIL', f'{sum(c["ok"] for c in checks)}/{len(checks)}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
