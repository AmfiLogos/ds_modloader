#!/usr/bin/env python3
"""Saves audit fixes #3 and #5 in the real game, on test copy K (hidden, muted, staged; take a lease first: rig.py lease
<lane> --pid). Uses this worktree's build/. Each failure is forced once.
 #3 a corrupt worlds.json: kept as worlds.json.bad-<time> (same bytes), not rewritten, and a load is refused
 #5 Apply at the title (soft restart into B with Hell difficulty on): B writes the new mod set into worlds.json
    ("modset_lines" lists hell-difficulty), which the multiplayer HELLO sends
Output: <out>/ (logs, summary.json)."""
import json, os, sys, time, glob, shutil, signal, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
T.X = 'K'; T.I = r.idir('K'); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/safety')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
WJ = os.path.join(T.MODS, 'worlds.json')
BROKEN = '{"worlds": {"f816ebc6-195c-432c-9fff-754b75ee3738": "gcd-cdr", broken'


def stop_all():
    for p in T.game_pids():
        try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        if T.alive(p): os.kill(p, signal.SIGKILL)


def main():
    for p in T.game_pids(): raise SystemExit(f'copy K already running: {p}')
    sv = os.path.join(T.I, 'data', 'Saves')                 # only the seed world
    if os.path.isdir(sv):
        old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True); shutil.move(sv, old)
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        # ---- #3
        dump = json.dump
        def broken(obj, f, *a, **k):                         # start_a writes worlds.json: make that one corrupt
            if getattr(f, 'name', '').endswith('worlds.json'): f.write(BROKEN)
            else: dump(obj, f, *a, **k)
        json.dump = broken
        try: T.start_a(enabled=('gcd-cdr', 'multiplayer'))
        finally: json.dump = dump
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(8)
        bad = glob.glob(WJ + '.bad-*')
        S['bad_files'] = [os.path.basename(b) for b in bad]
        check('#3 corrupt worlds.json kept as .bad-<time>, same bytes', len(bad) == 1 and open(bad[0]).read() == BROKEN, S['bad_files'])
        check('#3 not rewritten this session', not os.path.exists(WJ))
        mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0)
        m = V.wait_line(r'world check: REFUSED .* worlds\.json could not be read', mark, 30)
        time.sleep(3); st = T.status()
        check('#3 load refused with a message, still at the title', m is not None and st.get('scene') == 1, V.since(mark)[-300:])
        T.shot('3_refused')
        stop_all()
        for b in bad: shutil.move(b, os.path.join(OUT, os.path.basename(b)))
        # ---- #5
        a = T.start_a(enabled=('gcd-cdr', 'multiplayer'))
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        w0 = json.load(open(WJ)); S['modset_before'] = w0.get('modset_lines')
        check('#5 before: Hell not in the session set', not any('hell-difficulty' in x for x in w0.get('modset_lines', [])), S['modset_before'])
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr', 'hell-difficulty']})
        dt, b = T.wait_swap(a, t_req, 1)
        t0 = time.time(); w1 = {}
        while time.time() - t0 < 30:
            try: w1 = json.load(open(WJ))
            except Exception: pass
            if any('hell-difficulty' in x for x in w1.get('modset_lines', [])): break
            time.sleep(1)
        S['modset_after'] = w1.get('modset_lines'); S['modset_hash'] = (w0.get('modset'), w1.get('modset'))
        check('#5 after Apply, B wrote the new set (Hell in modset_lines, new modset hash)',
              any('hell-difficulty' in x for x in w1.get('modset_lines', [])) and w1.get('modset') != w0.get('modset'), S['modset_hash'])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        stop_all()
        check("owner's prefs + UserSetting identical (check_prefs)", r.check_prefs(restore=False))
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
