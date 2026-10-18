#!/usr/bin/env python3
"""TEST ONLY: the mod Settings page on a hidden test copy (never the owner's install, saves or screen).

Copy T (lrig instances), background-only, window staged far off every display, DSL_NOTCH=0; a guard kills it if a window
ever shows or it becomes frontmost. Takes a pair lease first (~/ds_multiplayer/rig/rig.py lease) and obeys the test copy
cap (lrig.cap_wait). The loader of THIS worktree's build/ + the test module settings-demo (demo/).
 1. English: Mods window, the demo's Settings button, its Settings page; then a number (slider), two switches and a
    choice are changed: the local switch is saved at once and the module is told; gameplay ones wait for Apply.
 2. Apply: the new game runs with the new gameplay values (the module logs them), settings.json has them, worlds.json
    modset_lines carries "settings-demo/<id>@<value>".
 3. Japanese: the same pages in Japanese.
Usage: ui_settings_test.py <out dir> [--no-apply]
"""
import glob, json, os, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(L, 'test'))
import lrig as r

X = 'T'; I = r.idir(X)
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'build', 'run')))
UI = os.path.join(OUT, 'ui'); MODS = os.path.join(I, 'data', 'Mods'); SDIR = os.path.join(I, 'softrestart')
APP = os.path.join(I, 'DungeonSettlers.app'); BUILD = os.path.join(L, 'build')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []; checks = []


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


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
                for p in pids: os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(0.5)


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def wait_for(pred, what, limit=180):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        time.sleep(0.3)
    raise RuntimeError('timeout: ' + what)


def install():
    import plistlib
    pl = plistlib.load(open(os.path.join(APP, 'Contents', 'Info.plist'), 'rb'))
    if not (pl.get('LSBackgroundOnly') and pl.get('LSUIElement')): raise SystemExit('refused: copy T is not background-only')
    fw = os.path.join(APP, 'Contents', 'Frameworks'); md = os.path.join(APP, 'Contents', 'Resources', 'mods')
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    d = os.path.join(md, 'settings-demo'); os.makedirs(d, exist_ok=True)
    so = os.path.join(d, 'settings-demo.dylib.tmp')
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-mmacosx-version-min=12.0', '-I' + os.path.join(L, 'sdk'), '-dynamiclib',
                    '-install_name', '@rpath/settings-demo.dylib', '-o', so, os.path.join(HERE, 'demo', 'settings_demo.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', so], capture_output=True); os.replace(so, so[:-4])
    shutil.copy2(os.path.join(HERE, 'demo', 'mod.json'), os.path.join(d, 'mod.json'))
    ld = os.path.join(fw, 'lang'); os.makedirs(ld, exist_ok=True)               # the loader's texts (Frameworks/lang)
    for f in glob.glob(os.path.join(L, 'lang', '*.json')): shutil.copy2(f, os.path.join(ld, os.path.basename(f)))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    log('installed loader + settings-demo in copy', X)


def start(lang):
    r.cap_wait('settings test copy ' + X)
    r.prepare_data(X)
    us = os.path.join(I, 'data', 'UserSetting.json'); s = json.load(open(us))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = lang; s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(us, 'w'), indent=1)
    # without the multiplayer module the game keeps its own data folder (the copy's bundle id under its HOME)
    gd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}')
    os.makedirs(gd, exist_ok=True); json.dump(s, open(os.path.join(gd, 'UserSetting.json'), 'w'), indent=1)
    os.makedirs(MODS, exist_ok=True)
    for f in glob.glob(os.path.join(MODS, '*.json')):              # a clean start: no values, no worlds, no pending set
        os.makedirs(os.path.join(MODS, 'old'), exist_ok=True); os.replace(f, os.path.join(MODS, 'old', os.path.basename(f) + '.%d' % time.time()))
    json.dump({'enabled': ['settings-demo'], 'order': ['settings-demo']}, open(os.path.join(MODS, 'active.json'), 'w'))
    os.makedirs(UI, exist_ok=True)
    for f in glob.glob(os.path.join(UI, '*.json')): os.replace(f, f + '.old')
    for f in ('dsload.log', 'dsload.prev.log'):                    # wait_for reads this start's log only
        if os.path.exists(os.path.join(I, f)): os.replace(os.path.join(I, f), os.path.join(I, f + '.old'))
    for f in glob.glob(os.path.join(SDIR, 'handoff.json*')):
        if not f.endswith('.old'): os.replace(f, f + '.old')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(MODS, 'active.json'), 'DSML_SESSION': os.path.join(MODS, 'worlds.json'), 'DSML_DATA': os.path.join(I, 'data'),
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0',
           'DSL_UI_TEST': UI, 'DSL_PREWARM': '0', 'DSL_LOG_DIR': I}
    env.update(r.soft_test_env())
    r.require_windowed(X)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('started pid', p.pid, 'language', lang)
    wait_for(lambda: 'win: Mods window ready' in dlog(), 'Mods window ready', 240)
    wait_for(lambda: 'settings-demo at load' in dlog(), 'module load line', 30)
    return p.pid


SEQ = [0]
def ui(op, **kw):
    SEQ[0] += 1; seq = str(SEQ[0]); kw.update({'op': op, 'seq': seq})
    tmp = os.path.join(UI, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 30:
        try:
            j = json.load(open(os.path.join(UI, 'result.json'), encoding='utf-8', errors='replace'))   # the registry cuts long texts at 160 bytes (a CJK character too)
            if j.get('seq') == seq: return j
        except Exception: pass
        time.sleep(0.1)
    raise RuntimeError('ui command timed out: %s' % op)


def control(state, name):
    for c in state.get('controls', []):
        if c['name'] == name: return c
    return None


def shot(name):
    j = ui('shot', name=name)
    src = os.path.join(UI, name + '.png')
    if j.get('ok') and os.path.exists(src): shutil.copy2(src, os.path.join(OUT, name + '.png'))
    log('shot', name, 'ok' if j.get('ok') else 'FAILED')


def open_window(tag):
    for _ in range(40):                                          # the kit's templates come with the title screen
        st = ui('open'); time.sleep(1.0); st = ui('state')
        if st.get('open') and control(st, 'heading'): break
    time.sleep(1.5)
    st = ui('state')
    check(f'{tag}: Mods window open with a Settings button for settings-demo', control(st, 'settings:settings-demo') is not None, st)
    shot(f'{tag}_1_mods')
    ui('click', target='settings:settings-demo'); time.sleep(1.5)
    st = ui('state')
    names = [c['name'] for c in st.get('controls', [])]
    check(f'{tag}: Settings page: 4 controls', all(f'set:{k}' in names for k in ('gold_mult', 'hard_rule', 'difficulty', 'show_bars')), names)
    shot(f'{tag}_2_settings')
    return st


def sample_hang(tag):                                            # a copy that stops answering: where its threads are
    for p in game_pids():
        subprocess.run(['sample', str(p), '3', '-file', os.path.join(OUT, f'{tag}_hang_{p}.txt')], capture_output=True)


def stop_all():
    for p in game_pids():
        try: os.kill(p, signal.SIGTERM)
        except OSError: pass
    t0 = time.time()
    while game_pids() and time.time() - t0 < 15: time.sleep(0.3)
    for p in game_pids():
        try: os.kill(p, signal.SIGKILL)
        except OSError: pass


class Skip(Exception): pass


def main():
    if game_pids(): raise SystemExit('copy T already runs: ' + str(game_pids()))
    os.makedirs(OUT, exist_ok=True)
    pair = subprocess.run([sys.executable, os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'lease', 'modsettings', '--pid', str(os.getpid())],
                          capture_output=True, text=True).stdout.strip().splitlines()
    pair = pair[-1] if pair else ''
    log('lease', pair or 'NONE')
    if not pair: raise SystemExit('no free pair lease: not started')
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
      install()
      try:
        # ---- English
        if '--ja-only' in sys.argv: raise Skip()
        a = start(0)
        check('module read the defaults at load', 'settings-demo at load: gold_mult=1 hard_rule=0 difficulty=normal show_bars=1' in dlog())
        open_window('en')
        ui('value', target='set:gold_mult', value=7); ui('click', target='set:hard_rule'); ui('value', target='set:difficulty', value=2)
        ui('click', target='set:show_bars'); time.sleep(1.5)
        st = ui('state'); status = (control(st, 'status') or {}).get('text', '')
        check('local switch: module told, live', 'local setting show_bars changed' in dlog() and 'settings-demo now: gold_mult=1 hard_rule=0 difficulty=normal show_bars=0' in dlog())
        sj = os.path.join(MODS, 'settings.json')
        check('local switch saved at once, gameplay ones not yet', os.path.exists(sj) and '"show_bars":false' in open(sj).read() and 'gold_mult' not in open(sj).read(),
              open(sj).read() if os.path.exists(sj) else 'no settings.json')
        check('status names the gameplay changes', 'Gold multiplier: 7' in status and 'Hard' in status, status)
        shot('en_3_settings_changed')
        if '--no-apply' not in sys.argv:
            t0 = time.time(); ui('click', target='apply')
            wait_for(lambda: os.path.exists(os.path.join(SDIR, 'handoff.json.shown')) or os.path.exists(os.path.join(SDIR, 'handoff.json.failed')), 'swap', 180)
            check('Apply: the new game is on screen', os.path.exists(os.path.join(SDIR, 'handoff.json.shown')))
            wait_for(lambda: 'settings-demo at load: gold_mult=7 hard_rule=1 difficulty=hard show_bars=0' in dlog(), 'B values', 60)
            check('B runs with the applied values', True)
            time.sleep(3)
            sj_t = open(sj).read() if os.path.exists(sj) else ''
            check('settings.json has the applied values', '"gold_mult":7' in sj_t and '"difficulty":"hard"' in sj_t and '"hard_rule":true' in sj_t, sj_t)
            wj = open(os.path.join(MODS, 'worlds.json')).read() if os.path.exists(os.path.join(MODS, 'worlds.json')) else ''
            check('modset_lines carry the gameplay settings', 'settings-demo/gold_mult@7' in wj and 'settings-demo/difficulty@hard' in wj and 'show_bars' not in wj, wj[:600])
            for f in ('dsload.log', 'dsload.prev.log'):
                if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, 'en_' + f))
            time.sleep(4); shot('en_4_after_apply')
        stop_all()
      except Skip: pass
      if True:
        # ---- Japanese
        lang, tag = (3, 'de') if '--de' in sys.argv else (7, 'ja')          # --de: German (the longest button texts)
        start(lang)
        check(f'{tag}: language {lang}', f'language {lang} ({tag})' in dlog(), [l for l in dlog().splitlines() if 'language' in l][:3])
        try: st = open_window(tag)
        except RuntimeError: sample_hang(tag); raise
        if tag == 'ja':
            lab = (control(st, 'heading') or {}).get('text', '')
            check('ja: heading in Japanese', lab.startswith('設定'), lab)
        ui('click', target='set:hard_rule'); time.sleep(1.0); shot(f'{tag}_3_settings_changed')
        shutil.copy2(os.path.join(I, 'dsload.log'), os.path.join(OUT, f'{tag}_dsload.log'))
        stop_all()
    finally:
        stop_guard.set(); stop_all()
        subprocess.run([sys.executable, os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'release', pair], capture_output=True)
        log('lease released', pair)
    ok = all(c['ok'] for c in checks) and not violations
    json.dump({'checks': checks, 'violations': violations, 'ok': ok}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log('ALL PASS' if ok else 'SOME FAILED', f'{sum(c["ok"] for c in checks)}/{len(checks)}; guard violations {len(violations)}')


if __name__ == '__main__':
    main()
