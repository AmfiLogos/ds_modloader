#!/usr/bin/env python3
"""Escape closes our windows (owner 2026-10-25: "make sure Escape works to close our custom windows"; the kit's Escape
block in src/dsl_ui.c, kit 0.1.28). On a hidden Mac test copy (default M, DSL_TEST_COPY) with the loader, multiplayer
(only for its test control socket: status, load_save) and the test module uikit-demo (REAL Escape key events given to
the game's view, a fake mouse, the kit's Escape stack and which stock views show). Each case opens our window(s), presses
Escape through the game's own key path and checks: our top window is gone, the game did not act on that press (no Esc
menu in a world, the stock window under ours stays), and the next press does the next thing.
 title: demo panel; demo panel + its text field with the keyboard (1st press leaves the field, 2nd closes the panel);
        demo panel + the kit's notice box (1st = Cancel, 2nd closes the panel); Mods window; Mods window + its "Save these
        mods as a group" name row (1st = Cancel, 2nd closes); the stock Load list + a world's Mods window (only ours
        closes); Bug report window; a mod's Settings page in the Mods window (1st = its Back: the list again, the window
        stays; 2nd closes; owner 2026-10-25 "Go Back one level").
 world: Escape with none of ours opens the Esc menu (the key path works); demo panel (no Esc menu); Mods window (no Esc
        menu); Esc menu + Bug report (only ours closes); Esc menu > Load + a world's Mods window (only ours closes).
Guard (the Escape rule for every future window): a root panel made with no Escape registration logs ESC-MISSING; the
probe "Guard probe" must be caught, and no other ESC-MISSING line may appear in the whole run (sweep).
Old loaders fail: Escape leaves the demo panel and the notice open, and in a world the game also opens or closes its menus.
  esc_test.py [out] [--no-build] [--title-only]     Exit 0 = PASS. <out>/summary.json, logs. ESC_DYLIB=<another dsload.dylib>."""
import glob, json, os, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); LW = os.path.dirname(HERE); sys.path.insert(0, HERE)
import lrig as r
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(LW, 'test_env', 'l1_out', 'esc')))
TITLE_ONLY = '--title-only' in sys.argv
X = os.environ.get('DSL_TEST_COPY', 'M'); I = r.idir(X); APP = os.path.join(I, 'DungeonSettlers.app')
UDRV = os.path.join(OUT, 'uikit'); WDRV = os.path.join(OUT, 'win'); BDRV = os.path.join(OUT, 'bug')
DEMO = os.path.join(LW, 'build', 'test_mods', 'uikit-demo')
SAVE = 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0'; GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
ESC = 53                                                              # macOS key code of Escape
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []; violations = []; stop = threading.Event()
EXTRA_ENV = {}                                                        # a test that imports this one adds its own (kitlook_test: DSML_ROOT)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


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
    subprocess.run(f'cd "{LW}" && nice -n 19 zsh build.sh', shell=True, check=True, stdout=subprocess.DEVNULL)
    os.makedirs(DEMO, exist_ok=True)
    subprocess.run(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc '
                   f'-install_name @rpath/uikit-demo.dylib -o "{DEMO}/uikit-demo.dylib.tmp" test/mods_src/uikit-demo/uikit_demo.c '
                   f'&& codesign -f -s - "{DEMO}/uikit-demo.dylib.tmp" 2>/dev/null; mv -f "{DEMO}/uikit-demo.dylib.tmp" "{DEMO}/uikit-demo.dylib"',
                   shell=True, check=True)
    shutil.copy2(os.path.join(LW, 'test', 'mods_src', 'uikit-demo', 'mod.json'), DEMO)
    shutil.copy2(os.path.join(LW, 'modules', 'modmenu', 'slogel.png'), DEMO)


def install():
    if game_pids(): raise SystemExit(f'copy {X} is running: not installing over it')
    put(os.environ.get('ESC_DYLIB') or os.path.join(LW, 'build', 'dsload.dylib'), os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'))
    for src in (os.path.join(LW, 'build', 'mods', 'multiplayer'), DEMO):
        mid = os.path.basename(src)
        for root, _, files in os.walk(src):
            for f in files: put(os.path.join(root, f), os.path.join(APP, 'Contents', 'Resources', 'mods', mid, os.path.relpath(os.path.join(root, f), src)))
    s = os.path.join(LW, 'lang')
    if os.path.isdir(s):
        for f in os.listdir(s): put(os.path.join(s, f), os.path.join(APP, 'Contents', 'Frameworks', 'lang', f))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def start():
    r.require_background_only(X); r.prepare_data(X); r.require_windowed(X)
    pd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(os.path.join(r.SEED_DATA, 'Saves', SAVE + '.json'), os.path.join(I, 'data', 'Saves', SAVE + '.json'))
    mods = os.path.join(I, 'data', 'Mods'); os.makedirs(mods, exist_ok=True)
    for f in glob.glob(os.path.join(mods, 'worlds.json')):
        os.makedirs(os.path.join(mods, 'old'), exist_ok=True); os.replace(f, os.path.join(mods, 'old', 'worlds.json.%d' % time.time()))
    prof = os.path.join(I, 'esc_profile.json'); json.dump({'enabled': ['multiplayer', 'uikit-demo'], 'order': ['multiplayer', 'uikit-demo']}, open(prof, 'w'))
    for d, fs in ((UDRV, ('req.json', 'resp.json')), (WDRV, ('cmd.json', 'result.json')), (BDRV, ('cmd.json', 'result.json'))):
        os.makedirs(d, exist_ok=True)
        for f in fs:
            if os.path.exists(os.path.join(d, f)): os.replace(os.path.join(d, f), os.path.join(d, f + '.old'))
    crd = os.path.join(OUT, 'crashrep'); os.makedirs(crd, exist_ok=True); open(os.path.join(crd, 'answer'), 'w').write('no')
    if os.path.exists(r.sockpath(X)): os.replace(r.sockpath(X), r.sockpath(X) + '.old')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'),
           'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data'), 'DSML_SESSION': os.path.join(mods, 'worlds.json'),
           'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSL_UPDATE': '0',
           'DSL_CLOUD': '0', 'DSL_PREWARM': '0', 'DSL_CRASHREP_TEST': crd, 'DSL_BUGREP_DRY': '1',
           'DSL_UIKIT_TEST': UDRV, 'DSL_UI_TEST': WDRV, 'DSL_BUGREP_TEST': BDRV}
    env.update(r.soft_test_env()); env.update(EXTRA_ENV)
    ww, wh = r.win_size()                                              # LRIG_SIZE (default 960x540; panelsize_test: 800x600)
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', str(ww), '-screen-height', str(wh)]
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('started', p.pid)
    return p.pid


SEQ = [0]
def req(d, inname, outname, timeout, **kw):
    SEQ[0] += 1; kw['seq'] = SEQ[0] if inname == 'req.json' else str(SEQ[0])
    tmp = os.path.join(d, inname + '.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(d, inname))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            j = json.load(open(os.path.join(d, outname)))
            if str(j.get('seq')) == str(SEQ[0]): return j
        except Exception: pass
        time.sleep(0.05)
        if not game_pids(): raise RuntimeError('the game copy is gone (crash?)')
    return {'timeout': kw}
ui = lambda cmd, timeout=15, **kw: req(UDRV, 'req.json', 'resp.json', timeout, cmd=cmd, **kw)
win = lambda op, timeout=15, **kw: req(WDRV, 'cmd.json', 'result.json', timeout, op=op, **kw)
bug = lambda op, timeout=15, **kw: req(BDRV, 'cmd.json', 'result.json', timeout, op=op, **kw)
st = lambda: ui('state')


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def ctl(obj, timeout=20):
    try: return r.send(X, obj, timeout=timeout, connect_wait=0)
    except Exception as e: return {'error': str(e)}


def wait_for(pred, limit, step=0.25):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        time.sleep(step)
    return None


def press():
    """one real Escape (keyDown + keyUp on the game's view), then a few frames for the game and the kit to act"""
    j = ui('key', code=ESC); time.sleep(1.2); return j


def until(key_pred, limit=6):
    return wait_for(lambda: (lambda s: s if key_pred(s) else None)(st()), limit)


def notice_up(s): return s.get('w', {}).get('notice_ok', {}).get('on', -1) == 1


def title_cases():
    # 1. the demo panel
    ui('api', op='open'); s = until(lambda s: s.get('built') == 1)
    check('title 1: demo panel open and on the Escape stack', bool(s) and s.get('esc_n', 0) >= 1, s and s.get('esc'))
    press(); s = st()
    check('title 1: Escape closes the demo panel', s.get('built') == 0, s.get('esc'))
    check('title 1: the title menu is still there', s.get('titlemenu') == 1, s.get('titlemenu'))
    # 2. the demo panel's text field with the keyboard
    ui('api', op='open'); until(lambda s: s.get('built') == 1); time.sleep(0.5)
    ui('click', target='field'); s = until(lambda s: s.get('typing') == 1, 8)
    check('title 2: the demo field has the keyboard', bool(s), st().get('typing'))
    ui('type', text='abc'); time.sleep(0.3)
    press(); s = st()
    check('title 2: 1st Escape leaves the field, the panel stays', s.get('typing') == 0 and s.get('built') == 1, (s.get('typing'), s.get('built')))
    press(); s = st()
    check('title 2: 2nd Escape closes the panel', s.get('built') == 0, s.get('esc'))
    # 3. the kit's notice box over the demo panel
    ui('api', op='open'); until(lambda s: s.get('built') == 1); time.sleep(0.5)
    n0 = st().get('n', {}).get('cancel', 0)
    ui('click', target='notice'); s = until(notice_up, 8)
    check('title 3: the notice box shows over the panel', bool(s), st().get('esc'))
    press(); s = until(lambda s: s.get('n', {}).get('cancel', 0) == n0 + 1 and not notice_up(s), 4) or st()
    check('title 3: 1st Escape = the notice\'s Cancel (callback ran, box gone)', s.get('n', {}).get('cancel', 0) == n0 + 1 and not notice_up(s), (s.get('n'), s.get('esc')))
    check('title 3: the panel under the notice stays', s.get('built') == 1, s.get('esc'))
    press(); s = st()
    check('title 3: 2nd Escape closes the panel', s.get('built') == 0, s.get('esc'))
    # 4. the Mods window
    w = win('open'); w = wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10)
    check('title 4: Mods window open', bool(w), w)
    press(); w = win('state')
    check('title 4: Escape closes the Mods window', not w.get('open'), w)
    # 5. its name row ("Save these mods as a group")
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.5)
    win('click', target='group-new'); s = until(lambda s: s.get('esc_n', 0) >= 2, 4) or st()
    check('title 5: the name row is on the Escape stack over the window', s.get('esc_n', 0) >= 2, s.get('esc'))
    press(); s = st(); w = win('state')
    check('title 5: 1st Escape = the row\'s Cancel, the window stays', w.get('open') and s.get('esc_n') == 1, (w.get('open'), s.get('esc')))
    press(); w = win('state')
    check('title 5: 2nd Escape closes the window', not w.get('open'), w)
    # 6. the stock Load list with a world's Mods window over it
    ui('stock', what='title_load'); s = until(lambda s: s.get('loadlist') == 1, 8)
    check('title 6: the stock Load list shows', bool(s), st().get('loadlist'))
    win('open_world', guid=GUID, world='Elven Expedition'); w = wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10)
    check('title 6: the world\'s Mods window opens over it', bool(w), w)
    press(); w = win('state'); s = st()
    check('title 6: Escape closes the Mods window', not w.get('open'), w)
    check('title 6: the stock Load list under it stays', s.get('loadlist') == 1, s.get('loadlist'))
    press(); s = st(); log('info: next Escape on the stock Load list alone: loadlist', s.get('loadlist'))
    if s.get('loadlist') == 1: ui('stock', what='title_load')        # no harm: the list is open already
    # 7. the Bug report window
    if st().get('loadlist') == 1: press()
    ui('stock', what='title_bug'); b = wait_for(lambda: (lambda j: j if j.get('open') else None)(bug('state')), 10)
    check('title 7: Bug report window open', bool(b), b)
    press(); b = bug('state'); s = st()
    check('title 7: Escape closes the Bug report window', not b.get('open'), b)
    check('title 7: the title menu is still there', s.get('titlemenu') == 1, s.get('titlemenu'))
    # 9. the guard: a root panel with no Escape registration is caught (the rule for every future window)
    ui('api', op='bare', a=1); hit = wait_for(lambda: 'ESC-MISSING "Guard probe"' in dlog(), 8)
    check('title 9: the guard logs ESC-MISSING for a window with no Escape registration', bool(hit), 'no line')
    ui('api', op='bare', a=0)
    # 8. a mod's Settings page inside the Mods window (multiplayer's, a launcher mod): Escape = Back first
    names = lambda j: [c.get('name') for c in j.get('controls', [])]
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.5)
    win('click', target='tab-mods:launcher'); time.sleep(0.5); win('click', target='settings:multiplayer')
    w = wait_for(lambda: (lambda j: j if 'set-back' in names(j) else None)(win('state', dump=1)), 6) or win('state')
    check('title 8: the Settings page shows in the Mods window', 'set-back' in names(w), names(w)[:12])
    press(); w = win('state')
    check('title 8: 1st Escape = Back (the mod list again, the window stays)', w.get('open') and 'set-back' not in names(w) and 'settings:multiplayer' in names(w), (w.get('open'), names(w)[:12]))
    press(); w = win('state')
    check('title 8: 2nd Escape closes the window', not w.get('open'), w.get('open'))


def world_cases():
    # 0. none of ours: the game's own Escape (proves the key reaches the game at all)
    press(); s = until(lambda s: s.get('sysmenu') == 1, 4) or st()
    check('world 0: Escape with none of ours opens the Esc menu (the key path works)', s.get('sysmenu') == 1, s.get('sysmenu'))
    press(); s = until(lambda s: s.get('sysmenu') != 1, 4) or st()
    check('world 0: the next Escape closes it', s.get('sysmenu') != 1, s.get('sysmenu'))
    # 1. the demo panel
    ui('api', op='open'); until(lambda s: s.get('built') == 1)
    press(); time.sleep(0.8); s = st()
    check('world 1: Escape closes the demo panel', s.get('built') == 0, s.get('esc'))
    check('world 1: the Esc menu did not open', s.get('sysmenu') != 1, s.get('sysmenu'))
    # 2. the Mods window
    win('open'); w = wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10)
    check('world 2: Mods window open', bool(w), w)
    press(); time.sleep(0.8); w = win('state'); s = st()
    check('world 2: Escape closes the Mods window', not w.get('open'), w)
    check('world 2: the Esc menu did not open', s.get('sysmenu') != 1, s.get('sysmenu'))
    # 3. the Esc menu with the Bug report window over it
    press(); s = until(lambda s: s.get('sysmenu') == 1, 4)
    check('world 3: Esc menu open', bool(s), st().get('sysmenu'))
    ui('stock', what='esc_bug'); b = wait_for(lambda: (lambda j: j if j.get('open') else None)(bug('state')), 10)
    check('world 3: Bug report window open over the Esc menu', bool(b), b)
    time.sleep(1)
    press(); time.sleep(0.8); b = bug('state'); s = st()
    check('world 3: Escape closes the Bug report window', not b.get('open'), b)
    check('world 3: the Esc menu under it stays', s.get('sysmenu') == 1, s.get('sysmenu'))
    # 4. Esc menu > Load with a world's Mods window over the Load list
    if s.get('sysmenu') != 1: press(); until(lambda s: s.get('sysmenu') == 1, 4)
    ui('stock', what='esc_load'); s = until(lambda s: s.get('loadlist') == 1, 8)
    check('world 4: the stock Load list shows', bool(s), st().get('loadlist'))
    win('open_world', guid=GUID, world='Elven Expedition'); w = wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10)
    check('world 4: the world\'s Mods window opens over it', bool(w), w)
    press(); time.sleep(0.8); w = win('state'); s = st()
    check('world 4: Escape closes the Mods window', not w.get('open'), w)
    check('world 4: the stock Load list under it stays', s.get('loadlist') == 1, s.get('loadlist'))


def main():
    if game_pids(): raise SystemExit(f'copy {X} already running: {game_pids()}')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: build()
    install()
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
        start()
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 240, 1)
        check('title reached', bool(t), t)
        s = wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
        check('uikit-demo driver up', bool(s), s)
        time.sleep(4)
        title_cases()
        if not TITLE_ONLY:
            a = ctl({'cmd': 'load_save', 'name': SAVE}, 30); log('load_save', a)
            t = wait_for(lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 180, 1)
            check('world loaded', bool(t), t)
            time.sleep(8)
            world_cases()
    except Exception as e:
        check('no exception', False, repr(e))
    finally:
        try: ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for p in game_pids(): os.kill(p, signal.SIGKILL)
        stop.set()
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
    miss = [l for l in open(os.path.join(OUT, 'dsload.log'), errors='replace').read().splitlines()
            if ('ESC-MISSING' in l or 'UI-RULE ' in l) and 'Guard probe' not in l] if os.path.exists(os.path.join(OUT, 'dsload.log')) else ['no dsload.log']
    check('sweep: no ESC-MISSING or UI-RULE line (every window of ours has its Escape registration and follows the UI rules)', not miss, miss[:3])
    fails = [c for c in checks if not c['ok']]
    check('copy never on screen / frontmost', not violations, violations)
    json.dump({'checks': checks, 'violations': violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log(f'RESULT esc_test: {len(checks) - len(fails) - (1 if violations else 0)} PASS, {len(fails) + (1 if violations else 0)} FAIL')
    sys.exit(1 if fails or violations else 0)


if __name__ == '__main__':
    main()
