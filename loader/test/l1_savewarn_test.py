#!/usr/bin/env python3
"""Owner item 9: a mod whose saves need the loader (mod.json "save_dependent": hell-difficulty) shows a warning, on
test copy K (hidden, muted, staged; take a lease first). Uses this worktree's build/.
 1. Load list: the world (gcd-cdr) has no warning in its hover card
 2. its "Change" -> Mods window: Hell off = no warning line; Hell on = "Saves made with Hell difficulty may not work
    without the mod loader." under its switch
 3. "Save for this world" -> the world's hover card ends with that warning
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, shutil, signal, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/savewarn')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
X = os.environ.get('DSL_TEST_COPY', 'K')                # its own copy (S is shared with other lanes); make_instance.sh K
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
WARN = 'Saves made with Hell difficulty may not work without the mod loader.'


def card(name):
    mark = len(T.dlog()); m, text = V.tip('row:0', name)
    b = V.wait_line(r"reg test: tip row:0 body '([^']*)'", mark, 5)
    return b.group(1) if b else ''


def warn_text(st):
    return [c['text'] for c in st.get('controls', []) if c['name'] == 'savewarn:hell-difficulty']


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {T.X} already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    sv = os.path.join(T.I, 'data', 'Saves')                 # only the seed world on the list: row 0 = Elven Expedition
    if os.path.isdir(sv):
        old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True); shutil.move(sv, old)
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        T.start_a(extra_env={'DSL_UI_TEST': V.UI})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        r.send(T.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', T.wait_log('rows: Load list rebuilt', 20)); time.sleep(1.5)
        b0 = card('0_card_no_hell'); S['card_before'] = b0
        check('no warning without a save_dependent mod', b0 and 'Saves made with' not in b0, b0)
        T.request({'cmd': 'change', 'row': 0}); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        w = warn_text(st); S['warn_off'] = w
        check('Hell off: warning line hidden', w == [''], w)
        V.ui('click', target='mod:hell-difficulty'); time.sleep(0.5); st = V.ui('state')
        w = warn_text(st); S['warn_on'] = w
        check('Hell on: warning line under its switch', w == [WARN], w)
        T.shot('1_mods_window_hell_on')
        V.ui('click', target='apply'); time.sleep(1)
        wj = json.load(open(os.path.join(T.MODS, 'worlds.json'))).get('worlds', {})
        check('world saved with Hell', wj.get(T.GUID) == 'gcd-cdr+hell-difficulty', wj)
        V.ui('close'); time.sleep(1.5)
        b1 = card('2_card_hell'); S['card_after'] = b1
        check('hover card ends with the warning', b1.endswith('<color=#EA9F3D>%s</color>' % WARN), b1)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ['dsload.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
