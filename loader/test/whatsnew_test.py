#!/usr/bin/env python3
"""The "What's new" window (owner 2026-10-25, "All three"; src/dsl_whatsnew.c, kit 0.1.28): shown ONCE after the kit
version changed, with this release's CHANGELOG.md entry, on the kit window template. Hidden Mac test copy (default WNW,
DSL_TEST_COPY) with the loader, multiplayer (test control socket) and uikit-demo (real Escape key, the Escape stack), as
esc_test.py. Three starts of the same copy:
 1. <data>/Mods/whatsnew.txt = an older version, DSL_WHATSNEW_TEST=1: the window shows at the idle title with this
    release's CHANGELOG entry (the same bytes as CHANGELOG.md says), on the Escape stack; whatsnew.txt now names this
    release; one Escape closes it and the title menu stays.
 2. the same copy again: no window (shown before).
 3. whatsnew.txt = an older version again, without DSL_WHATSNEW_TEST: no window (a test copy never shows it), and the
    file is not changed.
Sweep: no UI-RULE or ESC-MISSING line in any start (the window follows the template's rules).
Fails: no window after a version change, a second window, a window in a test copy, a window without the entry, a
UI-RULE line, an Escape that leaves it open.
  whatsnew_test.py [out] [--no-build]     Exit 0 = PASS. <out>/summary.json, logs of each start."""
import os, re, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'WNW')
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(os.path.dirname(HERE), 'test_env', 'l1_out', 'whatsnew')))
sys.argv = [sys.argv[0], OUT] + [a for a in sys.argv[1:] if a.startswith('--')]
import esc_test as E                                                  # its build, install, copy start, drivers and guard
LW = os.path.dirname(HERE); REPO = os.path.dirname(LW)
sys.path.insert(0, os.path.join(REPO, 'tools', 'changelog'))
import changelog as C
log, check, st, wait_for = E.log, E.check, E.st, E.wait_for
WN = os.path.join(E.I, 'data', 'Mods', 'whatsnew.txt')
OLD = '0.0.1'


def release():
    h = open(os.path.join(LW, 'build', 'gen', 'dsl_whatsnew_data.h'), encoding='utf-8').read()
    return re.search(r'#define DSL_WHATSNEW_VER "([^"]+)"', h).group(1)


def entry_bytes(ver):
    secs, errs = C.parse(os.path.join(REPO, 'CHANGELOG.md'))
    e = next((e for e in secs.get('kit', {}).get('entries', []) if e['ver'] == ver), None)
    return len(C.display(e['body']).encode('utf-8')) if e else 0


def seed(v):
    os.makedirs(os.path.dirname(WN), exist_ok=True); open(WN, 'w').write(v + '\n')


def stored():
    try: return open(WN).read().strip()
    except OSError: return ''


def run(n, test_env):
    """start n: start, title, driver; returns the (live) log reader. The caller checks, then stop(n) saves the logs"""
    E.EXTRA_ENV.clear()
    if test_env: E.EXTRA_ENV['DSL_WHATSNEW_TEST'] = '1'
    E.start()
    t = wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(E.ctl({'cmd': 'status'}, 5)), 240, 1)
    check(f'start {n}: title reached', bool(t), t)
    s = wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
    check(f'start {n}: uikit-demo driver up', bool(s), s)


def stop(n):
    try: E.ctl({'cmd': 'quit'}, 5)
    except Exception: pass
    wait_for(lambda: not E.game_pids(), 15, 0.5)
    for p in E.game_pids(): os.kill(p, signal.SIGKILL)
    if os.path.exists(os.path.join(E.I, 'dsload.log')): shutil.copy2(os.path.join(E.I, 'dsload.log'), os.path.join(OUT, f'dsload_{n}.log'))
    if os.path.exists(os.path.join(E.I, 'player.log')): shutil.copy2(os.path.join(E.I, 'player.log'), os.path.join(OUT, f'player_{n}.log'))


def main():
    if E.game_pids(): raise SystemExit(f'copy {E.X} already running: {E.game_pids()}')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: E.build()
    E.install()
    ver = release(); want = entry_bytes(ver)
    check(f'this build has a CHANGELOG kit entry for {ver}', want > 0, ver)
    g = threading.Thread(target=E.guard, daemon=True); g.start()
    try:
        # 1. a version change: the window shows once, with the entry
        seed(OLD); run(1, True)
        shown = wait_for(lambda: re.search(r'whatsnew: window shown for (\S+) \(the CHANGELOG entry, (\d+) bytes\)', E.dlog()), 60, 0.5)
        check('start 1: the window shows at the title after a version change', bool(shown), [l for l in E.dlog().splitlines() if 'whatsnew' in l][-3:])
        if shown:
            check('start 1: it shows this release and its CHANGELOG entry', shown.group(1) == ver and int(shown.group(2)) == want, (shown.groups(), ver, want))
            time.sleep(1.5); s = st()
            check('start 1: the window is on the Escape stack (template)', s.get('esc_n', 0) >= 1, s.get('esc'))
            j = E.ui('capture', name='whatsnew'); src = os.path.join(E.UDRV, 'cap', 'whatsnew.png')   # local look only, never commit
            if j.get('rc', -1) > 0 and os.path.exists(src): shutil.copy2(src, os.path.join(OUT, f'whatsnew_{ver}.png'))
            check('start 1: whatsnew.txt names this release (remembered when shown)', stored() == ver, stored())
            n0 = E.dlog().count('whatsnew: window closed')
            E.press(); ok = wait_for(lambda: E.dlog().count('whatsnew: window closed') == n0 + 1, 5, 0.25); s = st()
            check('start 1: one Escape closes it', bool(ok), s.get('esc'))
            check('start 1: the title menu is still there', s.get('titlemenu') == 1, s.get('titlemenu'))
            time.sleep(3)
            check('start 1: it does not show again in the same start', E.dlog().count('whatsnew: window shown') == 1, E.dlog().count('whatsnew: window shown'))
        stop(1)
        # 2. the same version again: no window
        run(2, True); time.sleep(12); d = E.dlog()
        check('start 2: no window: shown before', 'whatsnew: window shown' not in d and f'whatsnew: {ver} was shown before' in d,
              [l for l in d.splitlines() if 'whatsnew' in l][-3:])
        stop(2)
        # 3. a test copy without DSL_WHATSNEW_TEST: never
        seed(OLD); run(3, False); time.sleep(12); d = E.dlog()
        check('start 3: no window in a test copy', 'whatsnew: window shown' not in d and 'whatsnew: test copy' in d,
              [l for l in d.splitlines() if 'whatsnew' in l][-3:])
        check('start 3: whatsnew.txt unchanged', stored() == OLD, stored())
        stop(3)
    except Exception as e:
        check('no exception', False, repr(e))
        stop('x')
    finally:
        E.stop.set()
    miss = []
    for n in (1, 2, 3):
        p = os.path.join(OUT, f'dsload_{n}.log')
        miss += [l for l in open(p, errors='replace').read().splitlines() if 'ESC-MISSING' in l or 'UI-RULE ' in l] if os.path.exists(p) else [f'no dsload_{n}.log']
    check('sweep: no ESC-MISSING or UI-RULE line in any start', not miss, miss[:3])
    check('copy never on screen / frontmost', not E.violations, E.violations)
    fails = [c for c in E.checks if not c['ok']]
    import json; json.dump({'checks': E.checks, 'violations': E.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log(f'RESULT whatsnew_test: {len(E.checks) - len(fails)} PASS, {len(fails)} FAIL')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
