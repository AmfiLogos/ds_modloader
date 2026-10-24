#!/usr/bin/env python3
"""Apply in the Mods window -> the new game opens the Mods window again; a later start does not (bug 2026-10-20).
Old loaders: with a prewarmed new game (B boots before Apply) the window did not open again, and the stale marker
file opened it at the NEXT, unrelated start. On a hidden test copy (default Q, DSL_TEST_COPY; take a lease first),
loader = this worktree's build/dsload.dylib, mods already in the copy (multiplayer, gcd-cdr).
 per mode (prewarm on, prewarm off): start, open the Mods window, switch gcd-cdr on, Apply -> B opens the window by
 itself; quit B; start again -> the window stays closed.
  reopen_test.py [out]          Exit 0 = PASS. Output: <out>/summary.json, dsload logs."""
import glob, json, os, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
X = os.environ.get('DSL_TEST_COPY', 'Q')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(HERE), 'test_env', 'l1_out', 'reopen')))
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
BUILD = os.path.join(os.path.dirname(HERE), 'build')


def install():
    app = os.path.join(T.I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks')
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp'))
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def is_open(limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            if V.ui('state').get('open'): return True
        except Exception: pass
        time.sleep(1)
    return False


def quit_all(tag):
    for p in T.game_pids():
        try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        for _ in range(20):
            if not T.alive(p): break
            time.sleep(0.5)
        if T.alive(p): os.kill(p, signal.SIGKILL)
    for f in ['dsload.log', 'dsload.prev.log']:
        if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f'{tag}.{f}'))


def one_mode(pw, S):
    tag = 'prewarm' if pw else 'noprewarm'
    env = {'DSL_UI_TEST': V.UI, 'DSL_PREWARM': '1' if pw else '0'}
    T.clear_flags()
    a = T.start_a(enabled=('multiplayer',), extra_env=env, worlds={})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), f'{tag}: A title'); time.sleep(3)
    V.ui('open'); time.sleep(1.5)
    n1 = V.logs_all().count('prewarm B ready and waiting')
    if 'mod:gcd-cdr' not in {c['name'] for c in V.ui('state').get('controls', [])}:   # a fix (mod.json "category": "fix", owner
        V.ui('click', target='tab-mods:fixes'); time.sleep(1)                         # 2026-10-24) is on its own tab; this copy may keep an older one
    V.ui('click', target='mod:gcd-cdr')
    if pw: check(f'{tag}: prewarm B ready before Apply', V.wait_count('prewarm B ready and waiting', n1, 120))
    else: time.sleep(3)
    t_req = time.time(); V.ui('click', target='apply')
    dt, b = T.wait_swap(a, t_req, tag)
    used = ''.join(open(f, errors='replace').read() for f in glob.glob(os.path.join(OUT, f'round{tag}', 'dsload*.log')))
    check(f'{tag}: prewarmed game used = {pw}', ('the prewarmed B pid' in used) == pw)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), f'{tag}: B title', 90)
    check(f'{tag}: the new game opens the Mods window again', is_open(20))
    S[tag + '_swap_s'] = round(dt, 2)
    quit_all(tag + '1')
    T.start_a(enabled=('multiplayer', 'gcd-cdr'), extra_env=env, worlds={})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), f'{tag}: next start title')
    check(f'{tag}: the next start does NOT open the Mods window', not is_open(12))
    quit_all(tag + '2')


def main():
    if T.game_pids(): raise SystemExit(f'copy {X} already running')
    os.makedirs(T.SDIR, exist_ok=True); install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'checks': T.checks, 'violations': T.violations}
    try:
        for pw in (True, False): one_mode(pw, S)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set(); quit_all('end')
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
