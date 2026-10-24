#!/usr/bin/env python3
"""A mod zip dropped while the game runs (owner v13.2: "a refresh function if a new mod is added"), on test copy S
(hidden, muted, staged; take a lease first). Uses this worktree's build/.
 1. gcd-cdr is NOT installed; the game is at the title
 2. gcd-cdr-<v>.zip goes into the player's mods folder; the Mods window opens: the zip is installed, gcd-cdr is listed
 3. a zip that is not a mod + "Refresh": moved to .failed, one line for the player
 4. gcd-cdr on + Apply: the new game runs gcd-cdr
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, glob, shutil, signal, threading, subprocess, zipfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/pkg')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
UM = os.path.join(T.I, 'usermods')
APPMODS = os.path.join(T.I, 'DungeonSettlers.app', 'Contents', 'Resources', 'mods')


def park(p):
    if os.path.exists(p):
        d = os.path.join(T.I, 'old'); os.makedirs(d, exist_ok=True); os.replace(p, os.path.join(d, os.path.basename(p) + '.%d' % time.time()))


def state():
    return V.ui('state')


def control(st, name):
    c = [x for x in st.get('controls', []) if x['name'] == name]; return c[0] if c else None


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    park(os.path.join(APPMODS, 'gcd-cdr')); park(UM); os.makedirs(UM)
    ver = json.load(open(os.path.join(V.BUILD, 'mods', 'gcd-cdr', 'mod.json')))['version']
    zp = os.environ.get('PKG_ZIP', os.path.expanduser(f'~/ds_launcher/loader/build/packages/gcd-cdr-{ver}.zip'))   # the real package
    if not os.path.exists(zp):
        zp = os.path.join(OUT, f'gcd-cdr-{ver}.zip')
        subprocess.run(['/usr/bin/ditto', '-c', '-k', '--keepParent', os.path.join(V.BUILD, 'mods', 'gcd-cdr'), zp], check=True)
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        a = T.start_a(extra_env={'DSL_UI_TEST': V.UI, 'DSML_USER_MODS_DIR': UM})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        check('gcd-cdr not installed at start', 'reg: running gameplay set vanilla' in T.dlog() and not os.path.exists(os.path.join(APPMODS, 'gcd-cdr')))
        shutil.copy2(zp, os.path.join(UM, os.path.basename(zp)))       # the player drops the zip while the game runs
        V.ui('open'); st = {}
        for _ in range(15):
            time.sleep(1); st = state()
            if st.get('open'): break
        S['note_open'] = control(st, 'status')
        check('note: 1 new mod, turn it on + Apply', control(st, 'status') and 'Found 1 new mod' in control(st, 'status')['text'], control(st, 'status'))
        V.ui('click', target='tab-mods:fixes'); time.sleep(1); st = state()   # gcd-cdr is a fix: its own tab (owner 2026-10-24)
        check('Mods window lists the new mod', control(st, 'mod:gcd-cdr') is not None and control(st, 'mod:gcd-cdr')['on'] == 0, [c['name'] for c in st.get('controls', [])])
        check('zip installed (.installed, loader copy with marker)', os.path.exists(os.path.join(UM, '.installed', os.path.basename(zp))) and os.path.exists(os.path.join(APPMODS, 'gcd-cdr', '.player_mod')))
        T.shot('2_new_mod')
        open(os.path.join(UM, 'not-a-mod.zip'), 'wb').write(b'this is not a zip')
        V.ui('click', target='mods-refresh'); time.sleep(1.5); st = state()
        S['note_refresh'] = control(st, 'status')
        check('Refresh: bad zip -> .failed with one line', os.path.exists(os.path.join(UM, '.failed', 'not-a-mod.zip')) and 'not-a-mod.zip is not a mod' in (control(st, 'status') or {}).get('text', ''), control(st, 'status'))
        T.shot('3_refresh_failed')
        T.clear_flags()
        V.ui('click', target='mod:gcd-cdr'); time.sleep(0.5)
        t_req = time.time(); V.ui('click', target='apply')
        dt, b = T.wait_swap(a, t_req, 1)
        S['apply_seconds'] = round(dt, 2)
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 60); time.sleep(2)
        L = T.dlog()
        check('after Apply the new game runs gcd-cdr', 'reg: running gameplay set gcd-cdr' in L and 'gcd-cdr' in json.load(open(os.path.join(T.MODS, 'active.json'))).get('enabled', []))
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
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)   # any FAIL, guard violation or no check: exit 1
