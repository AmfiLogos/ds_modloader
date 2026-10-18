#!/usr/bin/env python3
"""TEST ONLY: crash reports in one hidden Mac test copy (never the owner's install, saves or screen).

  crash_rig_test.py <out dir> [instance letter, default L]

The copy must exist (test/make_instance.sh L). This branch's build/dsload.dylib (+ lang files) goes into its Frameworks.
A built multiplayer module (CRASHREP_MP_MODULE, a module folder) is loaded: its rig layer keeps the hidden window ordered
out and blocks full screen (DSRIG_HIDEWIN, lrig start --window); lrig's guard kills the copy if a window ever shows or it
comes to the front. Background-only app, DSL_NOTCH=0.
Steps:
  1. English: the consent notice shows at the title -> shot consent_en.png. Then the copy is killed with SIGSEGV
     (a simulated crash) before any answer.
  2. Japanese: the start finds the crashed session and queues a "crash" report; the notice shows again -> shot
     consent_ja.png; answer Yes -> the report is sent to the receiver.
  3. The report is read back with crashrep/tools/dscr_read.py (Keychain token) and checked: header, kind, install id,
     the previous session's dsload.log, and none of this Mac's names (user name, full name, computer name) in it.
  4. A normal quit (Application.Quit) leaves an END mark, and the next start queues nothing.
"""
import getpass, glob, json, os, pwd, signal, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))   # loader/test/lrig.py
import lrig as r

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out'))
X = sys.argv[2] if len(sys.argv) > 2 else 'L'
I = r.idir(X)
CTL, CROOT, MODS = os.path.join(OUT, 'ctl'), os.path.join(OUT, 'root'), os.path.join(OUT, 'mods')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
results = {'checks': []}
LANG = {'en': 0, 'ja': 7}
want_lang = 0


def check(name, ok, detail=''):
    results['checks'].append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]})
    log('PASS' if ok else 'FAIL', name, detail if not ok else '')


def install():
    fw = os.path.join(I, 'DungeonSettlers.app', 'Contents', 'Frameworks')
    if not os.path.isdir(fw):
        raise SystemExit(f'no test copy {X}: run test/make_instance.sh {X}')
    src = os.path.join(REPO, 'loader', 'build', 'dsload.dylib')
    subprocess.run(['cp', src, os.path.join(fw, 'dsload.dylib.tmp')], check=True)
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    ld = os.path.join(fw, 'lang'); os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(REPO, 'loader', 'lang', '*.json')):
        subprocess.run(['cp', f, os.path.join(ld, os.path.basename(f))], check=True)
    for d in (CTL, CROOT, MODS):
        os.makedirs(d, exist_ok=True)
    mp = os.environ.get('CRASHREP_MP_MODULE', os.path.expanduser('~/ds_int013/ds_modloader/loader/build/mods/multiplayer'))
    if not os.path.isdir(os.path.join(MODS, 'multiplayer')):
        subprocess.run(['cp', '-R', mp, os.path.join(MODS, 'multiplayer')], check=True)
    json.dump({'enabled': ['multiplayer']}, open(os.path.join(MODS, 'profile.json'), 'w'))


orig_prepare = r.prepare_data
def prepare(x):                                     # lrig's own settings (muted, small window), then the language
    orig_prepare(x)
    p = os.path.join(r.idir(x), 'data', 'UserSetting.json')
    s = json.load(open(p)); s['GeneralSettingData']['SaveCurrentLanguageType'] = want_lang; json.dump(s, open(p, 'w'), indent=1)
r.prepare_data = prepare


def state():
    try:
        return json.load(open(os.path.join(CTL, 'state.json')))
    except Exception:
        return {}


def wait(pred, what, timeout=300):
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = state()
        if pred(st):
            return st
        if not r.alive(r.read_pid(X)):
            log(f'{what}: the copy is not running'); return None
        time.sleep(1)
    log(f'{what}: timeout ({state()})'); return None


def start(lang):
    global want_lang
    want_lang = LANG[lang]
    for f in ('state.json', 'answer', 'quit'):
        p = os.path.join(CTL, f)
        if os.path.exists(p):
            os.replace(p, p + '.old')
    os.environ.update({'DSL_CRASHREP': '1', 'DSML_ROOT': CROOT, 'DSL_CRASHREP_TEST': CTL, 'DSL_UPDATE': '0', 'DSL_NO_ALERT': '1',
                       'DSL_LOG_DIR': I, 'DSL_MODS_DIR': MODS, 'DSL_PROFILE': os.path.join(MODS, 'profile.json'), 'DSL_NOTCH': '0'})
    return r.start(X, window=True)        # hidden window: the multiplayer module's rig layer (DSRIG_HIDEWIN) keeps it
                                          # ordered out and never full screen; lrig's guard kills it if a window shows




def shot(name):
    p = os.path.join(CTL, name + '.png')
    if os.path.exists(p):
        os.replace(p, p + '.old')
    open(os.path.join(CTL, 'shot_' + name), 'w').close()
    for _ in range(60):
        if os.path.exists(p):
            return p
        time.sleep(0.5)
    return None


def wait_exit(pid, timeout=60):
    for _ in range(timeout * 2):
        if not r.alive(pid):
            return True
        time.sleep(0.5)
    return False


def dsload():
    try:
        return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError:
        return ''


def names_of_this_mac():
    n = {getpass.getuser(), pwd.getpwuid(os.getuid()).pw_name}
    full = (pwd.getpwuid(os.getuid()).pw_gecos or '').split(',')[0]
    n.update(w for w in full.split() if len(w) >= 3)
    for cmd in (['scutil', '--get', 'ComputerName'], ['scutil', '--get', 'LocalHostName']):
        v = subprocess.run(cmd, capture_output=True, text=True).stdout.strip()
        if v:
            n.add(v)
    return sorted(x for x in n if x)


def main():
    install()
    # 1. English: the notice, then a crash before any answer
    pid = start('en')
    st = wait(lambda s: s.get('asked') == 1 and s.get('notice') == 1, 'consent notice (en)')
    check('en: the consent notice shows at the first title', st, state())
    time.sleep(1.5)
    check('en: screenshot', shot('consent_en'))
    check('en: nothing is queued or sent before an answer', st and st.get('consent') == -1 and st.get('sent24h') == 0, st)
    os.kill(pid, signal.SIGSEGV)
    check('simulated crash: the copy ended (SIGSEGV)', wait_exit(pid))
    # 2. Japanese: the crash is found, the notice shows again, Yes sends it
    pid = start('ja')
    st = wait(lambda s: s.get('asked') == 1 and s.get('notice') == 1, 'consent notice (ja)')
    check('ja: the notice shows again (no answer yet)', st, state())
    log_ = dsload()
    check('ja: the start found the crashed session', 'crash: crash: the previous session ended without a normal quit' in log_,
          [l for l in log_.splitlines() if 'crash:' in l][:6])
    check('ja: one report queued, none sent before Yes', st and st.get('queue') == 1 and st.get('sent24h') == 0, st)
    time.sleep(1.5)
    check('ja: screenshot', shot('consent_ja'))
    open(os.path.join(CTL, 'answer'), 'w').write('yes')
    st = wait(lambda s: s.get('consent') == 1 and s.get('queue') == 0 and str(s.get('last', '')).startswith('sent'), 'report sent', 180)
    check('Yes: the report is sent', st, state())
    cons = json.load(open(os.path.join(CROOT, 'crash', 'consent.json')))
    check('Yes: consent.json says yes', cons.get('consent') == 'yes', cons)
    # 3. read it back with the lead's read tool
    rb = os.path.join(OUT, 'readback'); os.makedirs(rb, exist_ok=True)
    tool = os.path.join(REPO, 'crashrep', 'tools', 'dscr_read.py')
    lst = subprocess.run([sys.executable, tool, 'list', '--prefix', 'reports/'], capture_output=True, text=True)
    mine = [l for l in lst.stdout.splitlines() if f'-crash-{cons["install"][:8]}-' in l]
    check('read tool: the report is listed', mine, lst.stdout[-400:] + lst.stderr[-200:])
    text = ''
    if mine:
        path = mine[-1].split()[-1]
        g = subprocess.run([sys.executable, tool, 'get', path, '--out', rb], capture_output=True, text=True)
        tp = (g.stdout.strip().splitlines() or [''])[-1]
        if os.path.isfile(tp):
            text = open(tp, errors='replace').read()
        check('read tool: the report downloads', text, g.stdout[-300:] + g.stderr[-300:])
    check('report: header, kind, install id', text.startswith('DSCRASH 1\n') and '\nkind: crash\n' in text and f'install: {cons["install"]}' in text, text[:300])
    check('report: the crashed session\'s dsload.log is inside', '===== dsload.log' in text and 'dsload ' in text)
    left = [n for n in names_of_this_mac() if n.lower() in text.lower()]
    check('report: none of this Mac\'s names is inside', not left, f'{len(left)} name(s) left')
    check('report: no /Users/<name> path', all(seg.startswith(('<user>', 'Shared')) for seg in text.split('/Users/')[1:]))
    # 4. a normal quit is not a crash
    open(os.path.join(CTL, 'quit'), 'w').close()
    check('normal quit: the copy ended', wait_exit(pid))
    sess = os.path.join(CROOT, 'crash', 'sessions', f'{pid}.txt')
    body = open(sess).read() if os.path.exists(sess) else ''
    check('normal quit: END mark written', '\nEND ' in body, body)
    pid = start('en')
    st = wait(lambda s: s.get('title') == 1 and 'asked' in s, 'title (3rd start)', 240)
    time.sleep(3)
    log_ = dsload()
    check('3rd start: the previous session ended normally, nothing queued',
          'crash: the previous session ended normally' in log_ and state().get('queue') == 0, [l for l in log_.splitlines() if 'crash:' in l][:6])
    open(os.path.join(CTL, 'quit'), 'w').close()
    if not wait_exit(pid, 30):
        r.stop(X)
    results['prefs_unchanged'] = r.check_prefs()
    ok = all(c['ok'] for c in results['checks'])
    json.dump(results, open(os.path.join(OUT, 'crash_rig_results.json'), 'w'), indent=1)
    log('ALL PASS' if ok else 'SOME FAILED', f"{sum(c['ok'] for c in results['checks'])}/{len(results['checks'])}")
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        if r.alive(r.read_pid(X)):
            r.stop(X)
