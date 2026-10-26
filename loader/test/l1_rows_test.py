#!/usr/bin/env python3
"""API 3 from a module, on test copy S: rows-demo (test only) adds a load-row text, a filter with a toggle, a Mods tab
and sets session busy. Checks the Load list (hidden by the filter, shown with the toggle on, its text on the row),
Apply refused while busy, the tab registered. Same guard and stage as l1_test.py."""
import re, json, os, shutil, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l1_test as t

OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/rows')
t.OUT = OUT; os.makedirs(OUT, exist_ok=True)
MD = os.path.join(t.I, 'DungeonSettlers.app/Contents/Resources/mods')
SRC = os.environ.get('ROWS_DEMO_DIR') or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'build', 'test_mods', 'rows-demo')   # built from test/mods_src/rows-demo (rowsdemo() in the gate)


def main():
    d = os.path.join(MD, 'rows-demo'); os.makedirs(d, exist_ok=True)
    for f in os.listdir(SRC):
        shutil.copy2(os.path.join(SRC, f), os.path.join(d, f + '.tmp')); os.replace(os.path.join(d, f + '.tmp'), os.path.join(d, f))
    g = threading.Thread(target=t.guard, daemon=True); g.start()
    try:
        a = t.start_a(('multiplayer', 'rows-demo'), {'ROWS_DEMO_BUSY': '1'})
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(2)
        L = t.dlog()
        t.check('module got API 3', 'rows-demo: loadrow_add 0, loadlist_filter 0, menu_add_tab 0, ui yes' in L, [l for l in L.splitlines() if 'rows-demo' in l][-2:])
        t.check('tab registered', "reg: tab 'Demo' (demo, order 90) added by rows-demo" in L)
        t.check('session busy set', 'reg: session busy SET by rows-demo' in L)
        t.check('running set ignores the client mod', bool(re.search(r'reg: running gameplay set (vanilla|hell-difficulty) ', L)))   # title key: Hell runs always (dshell2)
        t.r.send(t.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        t.check('filter hides the world (toggle off)', t.wait_log('(1 hidden by filters), 1 text source(s), ', 20), [l for l in t.dlog().splitlines() if 'rows:' in l][-2:])
        time.sleep(1); t.shot('r1_hidden')
        t.request({'cmd': 'toggles', 'on': True})
        t.check('toggle on shows it', t.wait_log('(0 hidden by filters), 1 text source(s), ', 20), [l for l in t.dlog().splitlines() if 'rows:' in l][-2:])
        time.sleep(1); t.shot('r2_shown')
        t.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']})
        t.check('Apply refused while busy', t.wait_log('reg test: apply refused: a demo session runs', 10))
        t.request({'cmd': 'mods'})
        t.check('title Mods click reaches the loader', t.wait_log('reg: title Mods clicked', 10))
    except Exception as e:
        t.check('run finished', False, e)
    finally:
        t.stop_guard.set()
        for p in t.game_pids():
            try:
                t.r.send(t.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception:
                pass
            time.sleep(3)
            if t.alive(p):
                os.kill(p, 9)
        shutil.copy2(os.path.join(t.I, 'dsload.log'), os.path.join(OUT, 'dsload.log'))
        os.makedirs(os.path.join(t.I, 'old'), exist_ok=True)
        os.replace(d, os.path.join(t.I, 'old', 'rows-demo.%d' % time.time()))     # test module out of the copy again
        json.dump({'checks': t.checks, 'violations': t.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        t.log(f"{sum(c['ok'] for c in t.checks)}/{len(t.checks)} checks, violations {len(t.violations)}")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in t.checks) or t.violations or not t.checks else 0)   # any FAIL, guard violation or no check: exit 1
