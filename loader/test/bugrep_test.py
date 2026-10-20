#!/usr/bin/env python3
"""Bug report window (owner 2026-10-20): the game's own "Bug Report" buttons open OUR report window in the modded game,
never the developers' web form. On a hidden Mac test copy (default R, DSL_TEST_COPY) with the loader, multiplayer (only
for the test control socket: load a save, open the Esc menu) and the test module bugrep-probe (fake mouse; it COUNTS
Application.OpenURL and TitleMenuView.OpenSavesFolder and never runs them, so an old loader cannot open a browser).

 1. title: a fake click on the stock Bug Report button opens our window (fails on old loaders: the stock button calls
    OpenURL), with the frame shot taken before it; type a text + contact; Send; the report on disk is the agreed format
    (DSCRASH 1, kind bug, bug.txt, screenshot.jpg.b64 = a JPEG, logs scrubbed: no home path / user name).
 2. world: load the seed save, open the Esc menu, a fake click on its Bug Report button: our window, the screenshot shows
    the world (the Esc menu hidden for the shot), Send, Esc closes the window, the Esc menu is still there.
Send mode: --dry (default) = the report is written, not posted (DSL_BUGREP_DRY); --send = posted to the receiver with
the admin header (rate limits off; token from the Keychain, never printed).
  bugrep_test.py [out] [--no-build] [--send] [--title-only]     Exit 0 = PASS. <out>/summary.json, shots, logs."""
import base64, glob, gzip, json, os, re, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); LW = os.path.dirname(HERE); sys.path.insert(0, HERE)
import lrig as r
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(LW, 'test_env', 'l1_out', 'bugrep')))
SEND = '--send' in sys.argv; TITLE_ONLY = '--title-only' in sys.argv
X = os.environ.get('DSL_TEST_COPY', 'R'); I = r.idir(X); APP = os.path.join(I, 'DungeonSettlers.app')
PDRV = os.path.join(OUT, 'probe'); BDRV = os.path.join(OUT, 'win'); PROBE = os.path.join(LW, 'build', 'test_mods', 'bugrep-probe')
SAVE = 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0'
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []; violations = []; stop = threading.Event()
HOME = os.path.expanduser('~'); USER = os.path.basename(HOME)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def game_pids():
    out = subprocess.run(['pgrep', '-f', '^' + os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def owner_playing():
    """the owner's own game: his launcher's copy or his app (a real game binary, not a shell that names the path)"""
    out = subprocess.run(['ps', '-axo', 'command='], capture_output=True, text=True).stdout
    roots = (os.path.join(HOME, 'Library/Application Support/DSModLauncher/games/'), os.path.join(HOME, 'Applications/Dungeon Settlers Modded.app/'),
             '/Applications/Dungeon Settlers Modded.app/')
    return [l for l in out.splitlines() if l.startswith(roots)]


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
    src = os.path.join(HERE, 'mods_src', 'bugrep-probe')
    subprocess.run(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation '
                   f'-install_name @rpath/bugrep-probe.dylib -o "{PROBE}/bugrep-probe.dylib.tmp" "{src}/bugrep_probe.c" '
                   f'&& codesign -f -s - "{PROBE}/bugrep-probe.dylib.tmp" 2>/dev/null; mv -f "{PROBE}/bugrep-probe.dylib.tmp" "{PROBE}/bugrep-probe.dylib"',
                   shell=True, check=True)
    shutil.copy2(os.path.join(src, 'mod.json'), PROBE)
    subprocess.run(f'cd "{LW}" && LOADER_ONLY=1 zsh build.sh', shell=True, check=True)


def install():
    if game_pids(): raise SystemExit(f'copy {X} is running: not installing over it')
    put(os.environ.get('BUGREP_DYLIB') or os.path.join(LW, 'build', 'dsload.dylib'), os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'))   # BUGREP_DYLIB: an old loader (the test must fail there)
    for f in os.listdir(PROBE): put(os.path.join(PROBE, f), os.path.join(APP, 'Contents', 'Resources', 'mods', 'bugrep-probe', f))
    for d in ('lang',):                                                # the loader's strings next to the dylib
        s = os.path.join(LW, d)
        if os.path.isdir(s):
            for f in os.listdir(s): put(os.path.join(s, f), os.path.join(APP, 'Contents', 'Frameworks', d, f))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def admin_token():
    p = subprocess.run(['security', 'find-generic-password', '-s', 'ds-crashrep-admin', '-a', 'ds-crashrep', '-w'], capture_output=True, text=True)
    return p.stdout.strip()


def start():
    r.require_background_only(X); r.prepare_data(X); r.require_windowed(X)
    pd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    assert json.load(open(os.path.join(pd, 'UserSetting.json')))['GraphicSettingData']['SaveCurrentScreenType'] == 1
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(os.path.join(r.SEED_DATA, 'Saves', SAVE + '.json'), os.path.join(I, 'data', 'Saves', SAVE + '.json'))
    mods = os.path.join(I, 'data', 'Mods'); os.makedirs(mods, exist_ok=True)
    for f in glob.glob(os.path.join(mods, 'worlds.json')):
        os.makedirs(os.path.join(mods, 'old'), exist_ok=True); os.replace(f, os.path.join(mods, 'old', 'worlds.json.%d' % time.time()))
    prof = os.path.join(I, 'bugrep_profile.json'); json.dump({'enabled': ['multiplayer', 'bugrep-probe'], 'order': ['multiplayer', 'bugrep-probe']}, open(prof, 'w'))
    for d in (PDRV, BDRV): os.makedirs(d, exist_ok=True)
    for d, fs in ((PDRV, ('req.json', 'resp.json')), (BDRV, ('cmd.json', 'result.json'))):
        for f in fs:
            if os.path.exists(os.path.join(d, f)): os.replace(os.path.join(d, f), os.path.join(d, f + '.old'))
    crd = os.path.join(OUT, 'crashrep'); os.makedirs(crd, exist_ok=True); open(os.path.join(crd, 'answer'), 'w').write('no')
    if os.path.exists(r.sockpath(X)): os.replace(r.sockpath(X), r.sockpath(X) + '.old')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'),
           'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data'), 'DSML_SESSION': os.path.join(mods, 'worlds.json'),
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
           'DSL_NOTCH': '0', 'DSL_UPDATE': '0', 'DSL_CLOUD': '0', 'DSL_PREWARM': '0', 'DSL_CRASHREP': '1', 'DSL_CRASHREP_TEST': crd,
           'DSL_BUGPROBE': PDRV, 'DSL_BUGREP_TEST': BDRV}
    if SEND: env['DSL_BUGREP_ADMIN'] = admin_token()
    else: env['DSL_BUGREP_DRY'] = '1'
    env.update(r.soft_test_env())
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('started', p.pid, 'send mode', 'REAL (admin)' if SEND else 'dry run')
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
probe = lambda c, timeout=15: req(PDRV, 'req.json', 'resp.json', timeout, cmd=c)
win = lambda op, timeout=15, **kw: req(BDRV, 'cmd.json', 'result.json', timeout, op=op, **kw)


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def wait_for(pred, limit, step=0.3):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        time.sleep(step)
    return None


def ctl(obj, timeout=20):
    try: return r.send(X, obj, timeout=timeout, connect_wait=0)
    except Exception as e: return {'error': str(e)}


def report_check(tag, text_part):
    """the newest report folder: the agreed format, a JPEG, no personal data in the text"""
    st = win('state'); d = st.get('dir', '')
    rep = sorted(glob.glob(os.path.join(d, '[0-9]*', 'report.txt.gz')), key=os.path.getmtime)
    check(f'{tag}: report saved locally', bool(rep), d)
    if not rep: return
    gz = open(rep[-1], 'rb').read(); shutil.copy2(rep[-1], os.path.join(OUT, f'{tag}_report.txt.gz'))
    t = gzip.decompress(gz).decode('utf-8', 'replace')
    check(f'{tag}: gzip under 4 MB', len(gz) < 4 << 20, len(gz))
    check(f'{tag}: first line DSCRASH 1, kind bug, reason', t.startswith('DSCRASH 1\n') and '\nkind: bug\n' in t and '\nreason: player bug report\n' in t)
    check(f'{tag}: contact line as typed', '\ncontact: tester#1234\n' in t, re.findall(r'\ncontact: [^\n]*', t))
    m = re.search(r'===== bug\.txt \((\d+) of (\d+) bytes\) =====\n', t)
    check(f'{tag}: bug.txt holds the typed text', bool(m) and text_part in t[m.end():m.end() + int(m.group(1)) + 1], m.group(0) if m else 'no bug.txt')
    s = re.search(r'===== screenshot\.jpg\.b64 \((\d+) of \d+ bytes, base64\) =====\n', t)
    img = b''
    if s:
        e = t.find('\n=====', s.end()); img = base64.b64decode(t[s.end():e if e > 0 else len(t)])
        open(os.path.join(OUT, f'{tag}_sent_screenshot.jpg'), 'wb').write(img)
    check(f'{tag}: screenshot section = a JPEG <= 1 MB', img[:3] == b'\xff\xd8\xff' and len(img) <= 1 << 20, len(img))
    rest = (t[:s.start()] + t[t.find('\n=====', s.end()):]) if s else t
    check(f'{tag}: logs present', '===== dsload.log (' in rest and '===== player.log (' in rest)
    leaks = [w for w in (HOME, '/Users/' + USER) if w in rest]
    check(f'{tag}: no home path / user name in the text', not leaks, leaks)


def main():
    if owner_playing(): raise SystemExit('the owner plays: no test copy now')
    if game_pids(): raise SystemExit(f'copy {X} already running: {game_pids()}')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: build()
    install()
    g = threading.Thread(target=guard, daemon=True); g.start()
    S = {'violations': violations, 'checks': checks}
    pid = None
    try:
        pid = start()
        ok = wait_for(lambda: 'probe: hooks ok' in dlog() or 'probe: hooks FAILED' in dlog(), 120)
        check('probe hooks on (OpenURL / OpenSavesFolder counted, never run)', 'probe: hooks ok' in dlog(), 'see dsload.log')
        if 'probe: hooks ok' not in dlog(): raise RuntimeError('probe hooks off: a stock click could open a browser; stopped')
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 240, 1)
        check('title reached', bool(t), t)
        time.sleep(4)
        # 1. title
        p = wait_for(lambda: (lambda j: j if j.get('rc') == 0 else None)(probe('click_title')), 30, 1)
        check('title Bug Report button found and clicked (fake mouse)', bool(p), p)
        w = wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 20, 0.5)
        check('title: OUR window opens', bool(w) and w.get('src') == 1, w)
        pr = probe('state')
        check('title: the stock form did not open (no OpenURL, no saves folder)', pr.get('openurl') == 0 and pr.get('folder') == 0, pr)
        S['title_state'] = w
        if not w: raise RuntimeError('no bug report window (old loader?): stopped')
        check('title: frame shot taken before the window (JPEG <= 1 MB)', bool(w) and w.get('shot') == 1 and 0 < w.get('shot_bytes', 0) <= 1 << 20, w)
        if w and os.path.exists(w.get('shot_path', '')): shutil.copy2(w['shot_path'], os.path.join(OUT, '1_title_preshot.jpg'))
        TXT = 'the shop froze when i opened it'          # typed keys come without shift (Event.KeyboardEvent)
        j = win('type', text=TXT); win('contact', text='tester#1234'); time.sleep(1)
        st = win('state'); check('title: typed text in the field', st.get('text') == TXT, st.get('text'))
        win('set_text', text=TXT + '\nLine two: I expected to buy a sword.\nLine three.'); time.sleep(0.5)
        win('shot', name='1_title_window'); time.sleep(1)
        win('send')
        d = wait_for(lambda: (lambda j: j if j.get('status') and not j.get('sending') and j.get('status') != 'Sending...' else None)(win('state')), 120, 1)
        S['title_after_send'] = d
        good = 'dry' in (d or {}).get('status', '').lower() or 'Thanks' in (d or {}).get('status', '')
        check('title: Send ends with the thanks line (id)', bool(d) and 'Thanks' in d.get('status', '') and d.get('id'), d)
        time.sleep(1); win('shot', name='2_title_sent'); time.sleep(1)
        report_check('title', 'the shop froze when i opened it\nLine two')
        win('esc'); time.sleep(1)
        check('title: Esc closes the window', not win('state').get('open'))
        if TITLE_ONLY: return S
        # 2. world
        a = ctl({'cmd': 'load_save', 'name': SAVE}, 30); log('load_save', a)
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 180, 1)
        check('world loaded', bool(t), t)
        time.sleep(8)
        ctl({'cmd': 'mpm_esc'}); time.sleep(2)
        win('shot', name='3_world_escmenu'); time.sleep(1)
        p = wait_for(lambda: (lambda j: j if j.get('rc') == 0 else None)(probe('click_esc')), 20, 1)
        check('Esc menu Bug Report button found and clicked', bool(p), p)
        w = wait_for(lambda: (lambda j: j if j.get('open') and j.get('src') == 2 else None)(win('state')), 20, 0.5)
        check('world: OUR window opens from the Esc menu', bool(w), w)
        pr = probe('state'); check('world: the stock form did not open', pr.get('openurl') == 0, pr)
        check('world: in-world screenshot taken', bool(w) and w.get('shot') == 1, w)
        if w and os.path.exists(w.get('shot_path', '')): shutil.copy2(w['shot_path'], os.path.join(OUT, '4_world_preshot.jpg'))
        win('type', text='units walk through walls'); win('contact', text='tester#1234'); time.sleep(1)
        win('set_text', text='units walk through walls\nafter the second floor'); time.sleep(0.5)   # typed keys do not redraw an unfocused field
        win('shot', name='5_world_window'); time.sleep(1)
        win('send')
        d = wait_for(lambda: (lambda j: j if j.get('status') and not j.get('sending') and j.get('status') != 'Sending...' else None)(win('state')), 120, 1)
        check('world: Send ends with the thanks line', bool(d) and 'Thanks' in d.get('status', ''), d)
        report_check('world', 'units walk through walls\nafter the second floor')
        time.sleep(1); win('shot', name='6_world_sent'); time.sleep(1)
        win('esc'); time.sleep(1.5)
        check('world: Esc closes our window', not win('state').get('open'))
        win('shot', name='7_world_after_close'); time.sleep(1)
        check('world: no stock form at any time', probe('state').get('openurl') == 0)
    except Exception as e:
        check('no exception', False, repr(e))
    finally:
        try: ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for q in game_pids():
            try: os.kill(q, signal.SIGKILL)
            except OSError: pass
        stop.set()
        for f in ('dsload.log', 'player.log', 'rig.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
        check('guard: no window on screen, never frontmost', not violations, violations)
        S['pass'] = all(c['ok'] for c in checks)
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
        log('RESULT', 'PASS' if S['pass'] else 'FAIL', f"{sum(c['ok'] for c in checks)}/{len(checks)}")
    return S


if __name__ == '__main__':
    s = main()
    sys.exit(0 if s and s.get('pass') else 1)
