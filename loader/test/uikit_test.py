#!/usr/bin/env python3
"""dsl_ui kit proof (loader/src/dsl_ui.c) on test copy L only (never the owner's install, saves, clipboard or screen).

  uikit_test.py [out_dir] [--no-build] [--run=1|2]

Build: this worktree's loader (build.sh; src/dsl_ui.c added through EXTRA_CFLAGS until build.sh lists it) + the
test-only module test/mods_src/uikit-demo. Install into copy L (Frameworks/dsload.dylib, Resources/mods/<id>).
Run 1: kit alone (profile: uikit-demo). Run 2: with the multiplayer module (profile: multiplayer + uikit-demo, DSMP_TEST=1
so its clipboard goes to a file too). Every widget is driven through the stock input path (uikit-demo's driver, DSL_UIKIT_TEST: a fake mouse
in UnityEngine.Input, real key NSEvents) or by the DSLUi calls, and the game's own frame is captured (ScreenCapture) at each step.
Windows: every window sits on a stage far off every display (DSL_SOFT_STAGE), the app is never activated; a guard kills
the copy if a window with alpha > 0 is ever on a display or the copy becomes frontmost.
Output: <out>/run<N>/ (dsload.log, player.log, cap/*.png, steps.json) + <out>/summary.json.
"""
import json, os, shutil, signal, subprocess, sys, threading, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'L'
I = r.idir(X)
APP = os.path.join(I, 'DungeonSettlers.app')
LW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # loader/ of this worktree
STAGE = '-20000,-20000'
args = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(args[0] if args else f'~/ds_launcher/loader/test_env/uikit_out/{time.strftime("%Y%m%d-%H%M%S")}'))
os.makedirs(OUT, exist_ok=True)
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []
results = []


def sh(cmd, **kw):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)
    if p.returncode:
        raise SystemExit(f'failed ({p.returncode}): {cmd}\n{p.stdout[-2000:]}\n{p.stderr[-2000:]}')
    return p.stdout


# ------------------------------------------------------------------ build + install
def build():
    env = dict(os.environ)
    if 'dsl_ui.c' not in open(os.path.join(LW, 'build.sh')).read():
        env['EXTRA_CFLAGS'] = 'src/dsl_ui.c'
    log('build: loader + modules', '(dsl_ui.c through EXTRA_CFLAGS)' if 'EXTRA_CFLAGS' in env else '')
    out = sh(f'cd "{LW}" && nice -n 19 ./build.sh', env=env)
    log(out.strip().replace('\n', ' | '))
    d = os.path.join(LW, 'build', 'test_mods', 'uikit-demo'); os.makedirs(d, exist_ok=True)
    sh(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc '
       f'-install_name @rpath/uikit-demo.dylib -o "{d}/uikit-demo.dylib.tmp" test/mods_src/uikit-demo/uikit_demo.c '
       f'&& codesign -f -s - "{d}/uikit-demo.dylib.tmp" 2>/dev/null; mv -f "{d}/uikit-demo.dylib.tmp" "{d}/uikit-demo.dylib"')
    shutil.copy2(os.path.join(LW, 'test/mods_src/uikit-demo/mod.json'), d)
    shutil.copy2(os.path.join(LW, 'modules/modmenu/slogel.png'), d)
    log('built', d)


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)       # never overwrite a mapped file in place


def install():
    if game_pids():
        raise SystemExit('copy L is running: not installing over it')
    put(os.path.join(LW, 'build/dsload.dylib'), os.path.join(APP, 'Contents/Frameworks/dsload.dylib'))
    md = os.path.join(APP, 'Contents/Resources/mods')
    for src in (os.path.join(LW, 'build/mods/multiplayer'), os.path.join(LW, 'build/test_mods/uikit-demo')):
        mid = os.path.basename(src)
        for root, _, files in os.walk(src):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), src)
                put(os.path.join(root, f), os.path.join(md, mid, rel))
    sh(f'codesign -f -s - "{APP}/Contents/MacOS/Dungeon Settlers"')
    log('installed loader + multiplayer + uikit-demo in', APP)


# ------------------------------------------------------------------ the copy
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
        stop_guard.wait(1.0)


def start(rdir, mods, extra):
    r.prepare_data(X)
    if 'multiplayer' not in mods:
        # without MP, persistentDataPath is the copy's own home folder (MP redirects it to DSRIG_DIR/data): give it the
        # same windowed 960x540 settings, else the game opens full screen on the owner's display (guard kill 18:59)
        pd = os.path.join(I, 'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mpL'); os.makedirs(pd, exist_ok=True)
        for f in ('UserSetting.json', 'OutGame.json'):
            if os.path.exists(os.path.join(I, 'data', f)):
                if os.path.exists(os.path.join(pd, f)) and not os.path.exists(os.path.join(pd, f + '.orig')):
                    shutil.copy2(os.path.join(pd, f), os.path.join(pd, f + '.orig'))
                shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
        g = json.load(open(os.path.join(pd, 'UserSetting.json')))['GraphicSettingData']
        assert g['SaveCurrentScreenType'] == 1 and g['ResolutionWidth'] == 960, g
    prof = os.path.join(I, 'uikit_profile.json')
    json.dump({'enabled': mods, 'order': mods}, open(prof, 'w'))
    drv = os.path.join(rdir, 'drv'); os.makedirs(drv, exist_ok=True)
    for f in ('req.json', 'resp.json', 'clipboard.txt'):
        if os.path.exists(os.path.join(drv, f)):
            os.replace(os.path.join(drv, f), os.path.join(drv, f + '.old'))
    if os.path.exists(r.sockpath(X)):
        os.replace(r.sockpath(X), r.sockpath(X) + '.old')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents/Frameworks/dsload.dylib'),
           'DSL_PROFILE': prof, 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_UIKIT_TEST': drv}
    env.update(extra)
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
         '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('started pid', p.pid, mods)
    return p, drv


def stop(p):
    if p.poll() is None:
        p.send_signal(signal.SIGTERM)
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            p.kill(); p.wait(5)
    log('stopped, rc', p.returncode)


# ------------------------------------------------------------------ driver
class Drv:
    def __init__(self, d):
        self.d = d; self.seq = 0; self.steps = []

    def req(self, cmd, timeout=15, **kw):
        self.seq += 1
        q = dict(kw, seq=self.seq, cmd=cmd)
        tmp = os.path.join(self.d, 'req.json.tmp')
        open(tmp, 'w', encoding='utf-8').write(json.dumps(q, ensure_ascii=False)); os.replace(tmp, os.path.join(self.d, 'req.json'))
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                a = json.load(open(os.path.join(self.d, 'resp.json')))
                if a.get('seq') == self.seq:
                    self.steps.append({'req': q, 'resp': a})
                    return a
            except Exception:
                pass
            time.sleep(0.05)
        raise RuntimeError(f'no answer to {q}')

    def state(self):
        return self.req('state')

    def act(self, cmd, wait=0.6, **kw):
        a = self.req(cmd, **kw)
        time.sleep(wait)
        return a, self.state()

    def cap(self, name):
        a = self.req('capture', name=name)
        return a['rc'] > 0


def check(name, ok, detail=''):
    results.append({'step': name, 'ok': bool(ok), 'detail': detail})
    log(('PASS ' if ok else 'FAIL ') + name, detail)


def col(st):
    """title column -> [(label, active, sibling)]"""
    out = []
    for e in st.get('title_column', '').split(';'):
        if e.count('|') >= 2:
            l, a, s = e.rsplit('|', 2); out.append((l, int(a), int(s)))
    return out


def wait_title(D, extra=(), limit=240):
    t0 = time.time(); st = None
    while time.time() - t0 < limit:
        try:
            st = D.state()
            labels = [c[0] for c in col(st) if c[1]]
            if 'UI Kit Demo' in labels and all(x in labels for x in extra):
                log('title seen; window', st.get('win'), '- waiting 10 s for the fade in')
                time.sleep(10)
                return D.state()
        except Exception:
            pass
        time.sleep(1)
    raise RuntimeError(f'title with UI Kit Demo {extra} not seen in {limit} s: {st}')


def W(st, n):
    return st['w'][n]


def widgets(D, tag):
    """every widget through real clicks / keys and the API; tag = run name"""
    a, st = D.act('click', target='title:UI Kit Demo', wait=1.2)
    check(f'{tag} title button click opens the panel', st['built'] == 1 and st['n']['title'] >= 1, f"rc {a['rc']} at {a.get('at')} n {st['n']}")
    if st['built'] != 1:
        D.cap(f'{tag}_02_no_panel')
        raise RuntimeError('panel not opened: later steps need it')
    D.cap(f'{tag}_02_panel')
    on = {k: v['on'] for k, v in st['w'].items() if isinstance(v, dict)}
    check(f'{tag} panel / text / button / toggle / value / field / scroll / image built and shown',
          all(on[k] == 1 for k in ('panel', 'head', 'status', 'press', 'disabled', 'toggle', 'value', 'field', 'scroll', 'image', 'notice', 'close')), str(on))
    check(f'{tag} text widget draws its text', W(st, 'head')['text'] == 'UI Kit Demo', W(st, 'head')['text'])
    a, st = D.act('click', target='press')
    check(f'{tag} button: real click runs the callback', st['n']['click'] == 1 and 'clicks 1' in W(st, 'status')['text'], f"{st['n']} status '{W(st, 'status')['text']}'")
    a, st = D.act('click', target='disabled')
    check(f'{tag} set_enabled(0): a disabled button ignores the click', st['n']['disabled'] == 0, str(st['n']))
    a, st = D.act('click', target='toggle')
    check(f'{tag} toggle: click flips it + change callback', st['toggle'] == 1 and st['n']['toggle'] == 1, f"toggle {st['toggle']} n {st['n']['toggle']}")
    D.cap(f'{tag}_03_toggle_on')
    a, st = D.act('api', op='toggle_set', a=0, wait=0.3)
    check(f'{tag} toggle_set / toggle_get (no callback)', st['toggle'] == 0 and st['n']['toggle'] == 1, f"toggle {st['toggle']} n {st['n']['toggle']}")
    a, st = D.act('click', target='value.next')
    check(f'{tag} value row: > goes to the next value', st['value'] == 2 and W(st, 'value')['text'] == 'Fast', f"value {st['value']} '{W(st, 'value')['text']}'")
    D.act('click', target='value.prev'); a, st = D.act('click', target='value.prev')
    check(f'{tag} value row: < twice', st['value'] == 0 and W(st, 'value')['text'] == 'Slow' and st['n']['value'] == 3, f"value {st['value']} '{W(st, 'value')['text']}' n {st['n']['value']}")
    a, st = D.act('api', op='value_set', a=1, wait=0.3)
    check(f'{tag} value_set / value_get (no callback)', st['value'] == 1 and W(st, 'value')['text'] == 'Normal' and st['n']['value'] == 3, f"{st['value']} {st['n']['value']}")
    a, st = D.act('click', target='field', wait=0.8)
    a, st = D.act('type', text='owner 1', wait=0.5)          # Event.KeyboardEvent key names: lower case
    check(f'{tag} field: typed text (TMP_InputField.ProcessEvent)', st['field'] == 'owner 1', f"rc {a['rc']} field '{st['field']}'")
    a, st = D.act('key', code=36, wait=0.8)
    check(f'{tag} field: Enter (real key event) = submit callback', st['n']['submit'] == 1, str(st['n']))
    D.cap(f'{tag}_04_field')
    a, st = D.act('api', op='field_set', s='abc', wait=0.3)
    check(f'{tag} field_set / field_get', st['field'] == 'abc', st['field'])
    a, st = D.act('hover', target='press', wait=1.2)
    check(f'{tag} tooltip: pointer over the button -> the game tooltip shows our title', st['tip']['shown'] == 1 and st['tip']['title'] == 'Press me', str(st['tip']))
    D.cap(f'{tag}_05_tooltip')
    a, st = D.act('api', op='mouse_off', wait=0.6)
    check(f'{tag} tooltip hides when the pointer leaves', st['tip']['shown'] == 0, str(st['tip']))
    r0 = W(st, 'row1')['r']
    a, st = D.act('wheel', target='scroll', dy=-4, wait=1.0)
    r1 = W(st, 'row1')['r']
    check(f'{tag} scroll: the mouse wheel moves the list', r1[1] > r0[1] + 5, f'row1 y {r0[1]} -> {r1[1]} (rc {a["rc"]})')
    check(f'{tag} scroll: rows outside the viewport exist (content height)', W(st, 'row20')['on'] == 1, str(W(st, 'row20')))
    D.cap(f'{tag}_06_scrolled')
    im = W(st, 'image')['r']
    check(f'{tag} image_png: Slogel PNG shown', W(st, 'image')['on'] == 1 and im[2] - im[0] > 10, str(W(st, 'image')))
    a, st = D.act('click', target='notice', wait=1.0)
    check(f'{tag} notice: the game notice box opens (two buttons)', W(st, 'notice_ok')['on'] == 1 and W(st, 'notice_cancel')['on'] == 1,
          f"ok {W(st, 'notice_ok')} cancel {W(st, 'notice_cancel')}")
    D.cap(f'{tag}_07_notice')
    a, st = D.act('click', target='notice_ok', wait=1.0)
    check(f'{tag} notice: Yes runs ok()', st['n']['ok'] == 1 and W(st, 'notice_ok')['on'] != 1, f"{st['n']} box {W(st, 'notice_ok')['on']}")
    D.act('click', target='notice', wait=1.0)
    a, st = D.act('click', target='notice_cancel', wait=1.0)
    check(f'{tag} notice: No runs cancel()', st['n']['cancel'] == 1, str(st['n']))
    a, st = D.act('click', target='notice1', wait=1.0)
    check(f'{tag} notice: one button (cancel_label NULL)', W(st, 'notice_ok')['on'] == 1 and W(st, 'notice_cancel')['on'] != 1,
          f"ok {W(st, 'notice_ok')['on']} cancel {W(st, 'notice_cancel')['on']}")
    D.cap(f'{tag}_08_notice1')
    a, st = D.act('click', target='notice_ok', wait=1.0)
    check(f'{tag} notice: one-button OK', st['n']['ok'] == 2, str(st['n']))
    a, st = D.act('api', op='set_text', target='press', s='Pressed!', wait=0.3)
    check(f'{tag} set_text on a button', W(st, 'press')['text'] == 'Pressed!', W(st, 'press')['text'])
    w0 = W(st, 'press')['r']
    a, st = D.act('api', op='place', target='press', x=20, y=100, w=160, h=40, wait=0.3)
    w1 = W(st, 'press')['r']
    check(f'{tag} place: new size', (w1[2] - w1[0]) < (w0[2] - w0[0]) * 0.8 and abs(w1[0] - w0[0]) < 3, f'{w0} -> {w1}')
    a, st = D.act('api', op='set_active', target='disabled', a=0, wait=0.3)
    off = W(st, 'disabled')['on']
    a, st = D.act('api', op='set_active', target='disabled', a=1, wait=0.3)
    check(f'{tag} set_active 0 / 1', off == 0 and W(st, 'disabled')['on'] == 1, f"{off} -> {W(st, 'disabled')['on']}")
    D.act('api', op='set_enabled', target='disabled', a=1, wait=0.3)
    a, st = D.act('click', target='disabled')
    check(f'{tag} set_enabled(1): the button clicks again', st['n']['disabled'] == 1, str(st['n']))
    D.cap(f'{tag}_09_after_api')
    return st


def clipboard(D, tag, path):
    # run2: the multiplayer module's test clipboard file is ASCII only (mpmenu h_clip_set uses str_utf8)
    txt = f'dsl_ui clipboard {tag}' + (' \u00e9' if tag == 'run1' else '')
    D.act('api', op='clipboard_set', s=txt, wait=0.2)
    a, st = D.act('api', op='clipboard_get', wait=0.1)
    f = open(path, encoding='utf-8').read() if os.path.exists(path) else None
    check(f'{tag} clipboard_set / clipboard_get through GUIUtility.systemCopyBuffer (test file)', a.get('clip') == txt and f == txt,
          f"get '{a.get('clip')}' file '{f}'")


def close_reopen(D, tag):
    a, st = D.act('click', target='close', wait=1.0)
    check(f'{tag} destroy: Close removes the panel', st['built'] == 0 and W(st, 'panel')['on'] == -1, f"built {st['built']} panel {W(st, 'panel')}")
    a, st = D.act('click', target='title:UI Kit Demo', wait=1.2)
    check(f'{tag} the title button opens it again', st['built'] == 1 and st['n']['title'] >= 2, str(st['n']))
    D.cap(f'{tag}_10_reopened')


def collect(rdir):
    for f in ('dsload.log', 'player.log'):
        if os.path.exists(os.path.join(I, f)):
            shutil.copy2(os.path.join(I, f), os.path.join(rdir, f))
    lg = open(os.path.join(rdir, 'dsload.log'), errors='replace').read() if os.path.exists(os.path.join(rdir, 'dsload.log')) else ''
    return lg


def run1():
    rdir = os.path.join(OUT, 'run1'); os.makedirs(rdir, exist_ok=True)
    p, drv = start(rdir, ['uikit-demo'], {})
    D = Drv(drv)
    try:
        st = wait_title(D)
        c = col(st)
        check('run1 title: "UI Kit Demo" is the last button of the stock column, shown',
              c and c[-1][0] == 'UI Kit Demo' and c[-1][1] == 1, str(c))
        D.cap('run1_01_title')
        widgets(D, 'run1')
        clipboard(D, 'run1', os.path.join(drv, 'clipboard.txt'))
        close_reopen(D, 'run1')
    finally:
        json.dump(D.steps, open(os.path.join(rdir, 'steps.json'), 'w'), indent=1)
        stop(p)
        lg = collect(rdir)
    ui = [l for l in lg.splitlines() if ' ui: ' in l or 'uikit-demo' in l]
    open(os.path.join(rdir, 'ui_log_lines.txt'), 'w').write('\n'.join(ui))
    check('run1 dsload.log: kit ready, no refused hook', 'kit ready' in lg and 'refused' not in lg.lower(),
          next((l for l in ui if 'kit ready' in l), 'no "kit ready" line'))


def run2():
    rdir = os.path.join(OUT, 'run2'); os.makedirs(rdir, exist_ok=True)
    clip = os.path.join(I, 'clipboard.txt')
    if os.path.exists(clip):
        os.replace(clip, clip + '.old')
    p, drv = start(rdir, ['multiplayer', 'uikit-demo'], {'DSMP_TEST': '1'})
    D = Drv(drv)
    try:
        st = wait_title(D, extra=('Multiplayer',))
        c = col(st); labels = [x[0] for x in c]
        im = labels.index('Multiplayer'); ik = labels.index('UI Kit Demo')
        imo = labels.index('Mods') if 'Mods' in labels else -1
        # v13 (owner 2026-10-16 #2): the loader's buttons go right below "Multiplayer", by order (Mods 100, then the demo)
        check('run2 title: "Mods" then "UI Kit Demo" right below "Multiplayer"', c[im + 1][0] == 'Mods' and c[im + 2][0] == 'UI Kit Demo' and imo == im + 1 and ik == im + 2, str(c))
        D.cap('run2_01_title')
        a, st = D.act('click', target='title:UI Kit Demo', wait=1.2)
        check('run2 our title button works with the multiplayer module loaded (before its page)', st['built'] == 1, f"n {st['n']} at {a.get('at')} under {st['w'].get('ptr_under')}")
        D.act('api', op='destroy', target='panel', wait=0.5)
        a, st = D.act('click', target='title:Multiplayer', wait=1.5)
        c2 = {x[0]: x[1] for x in col(st)}
        check('run2 MP title clone "Multiplayer" still works (its page opens, the stock column and our button hide)',
              c2.get('Multiplayer') == 0 or c2.get('UI Kit Demo') == 0, str(col(st)))
        D.cap('run2_02_mp_page')
        back = [x[0] for x in col(st) if x[1] and x[0].lower().startswith('back')]
        a, st = D.act('click', target='title:' + (back[0] if back else 'Back'), wait=1.5)
        c3 = {x[0]: x[1] for x in col(st)}
        check('run2 MP "Back" returns; our button shows again', c3.get('UI Kit Demo') == 1 and c3.get('Multiplayer') == 1, str(col(st)))
        widgets(D, 'run2')
        clipboard(D, 'run2', clip)
        close_reopen(D, 'run2')
    finally:
        json.dump(D.steps, open(os.path.join(rdir, 'steps.json'), 'w'), indent=1)
        stop(p)
        lg = collect(rdir)
    ui = [l for l in lg.splitlines() if ' ui: ' in l or 'uikit-demo' in l]
    open(os.path.join(rdir, 'ui_log_lines.txt'), 'w').write('\n'.join(ui))
    refused = [l for l in lg.splitlines() if 'refused' in l.lower()]
    check('run2 dsload.log: kit ready with multiplayer loaded, no refused hook', 'kit ready' in lg and not refused,
          (refused[:3] if refused else next((l for l in ui if 'kit ready' in l), 'no "kit ready" line')))


def main():
    if '--no-build' not in sys.argv:
        build()
        install()
    g = threading.Thread(target=guard, daemon=True); g.start()
    runs = [a.split('=')[1] for a in sys.argv if a.startswith('--run=')] or ['1', '2']
    try:
        for n in runs:
            try:
                (run1 if n == '1' else run2)()
            except Exception as e:
                check(f'run{n} aborted', False, repr(e))
    finally:
        stop_guard.set()
    s = {'out': OUT, 'passed': sum(x['ok'] for x in results), 'failed': sum(not x['ok'] for x in results), 'violations': violations, 'results': results}
    json.dump(s, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log(f"done: {s['passed']} passed, {s['failed']} failed, guard violations {len(violations)}; {OUT}")
    sys.exit(1 if s['failed'] or violations else 0)


if __name__ == '__main__':
    main()
