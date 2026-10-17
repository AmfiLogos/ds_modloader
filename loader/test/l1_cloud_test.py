#!/usr/bin/env python3
"""Modded saves backup in the real game (owner v13 item 7, option d), on test copy K (hidden, muted, staged;
take a lease first: rig.py lease <lane> --pid). Uses this worktree's build/. The cloud is FAKE (test/cloud/fake_steam_api.c,
a folder under <out>): the real Steam Cloud is never read or written. The owner's save folder: hash before/after.
 1. the loader's cloud thread uploads the copy's modded save as dsmodded_<name> (same bytes) while the game runs
 2. a file the "game" owns in the fake cloud (no prefix) stays as it is
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, glob, shutil, signal, subprocess, threading, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
T.X = 'K'; T.I = r.idir('K'); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/cloud')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
MOON = 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'


def tree_sum(d):
    h = hashlib.sha256()
    for root, _, files in sorted(os.walk(d)):
        for f in sorted(files):
            p = os.path.join(root, f); h.update(p.encode()); h.update(open(p, 'rb').read())
    return h.hexdigest()[:16]


def main():
    for p in T.game_pids(): raise SystemExit(f'copy K already running: {p}')
    real0 = tree_sum(r.REAL_DATA)
    run = os.path.join(OUT, time.strftime('%Y%m%d-%H%M%S')); cloud = os.path.join(run, 'cloud'); gsaves = os.path.join(run, 'gamesaves')
    os.makedirs(cloud); os.makedirs(gsaves)
    fake = os.path.join(run, 'fake_steam_api.dylib')
    subprocess.run(['clang', '-O0', '-dynamiclib', '-o', fake, os.path.join(HERE, 'cloud', 'fake_steam_api.c')], check=True)
    open(os.path.join(cloud, 'Game_own_file.json'), 'w').write('the game auto-cloud file')
    shutil.copy2(os.path.join(r.SEED_DATA, 'Saves', MOON), gsaves)
    old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S'))                  # last run's state out of the way
    for f in ('cloud_state.tsv', 'cloud_backups'):
        if os.path.exists(os.path.join(T.MODS, f)): os.makedirs(old, exist_ok=True); shutil.move(os.path.join(T.MODS, f), old)
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        T.start_a(enabled=('gcd-cdr', 'multiplayer'), extra_env={'DSL_UI_TEST': V.UI, 'DSL_CLOUD_API': fake, 'FAKE_CLOUD_DIR': cloud,
                                                                   'DSL_CLOUD_EVERY': '3'})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
        local = os.path.join(T.I, 'data', 'Saves', T.SAVE + '.json'); remote = os.path.join(cloud, 'dsmodded_' + T.SAVE + '.json')
        t0 = time.time()
        while time.time() - t0 < 60 and not os.path.exists(remote): time.sleep(1)
        time.sleep(2)
        S['cloud_files'] = sorted(os.listdir(cloud))
        check('modded save uploaded to the (fake) cloud as dsmodded_<name>, same bytes',
              os.path.exists(remote) and open(remote, 'rb').read() == open(local, 'rb').read(), S['cloud_files'])
        check("the game's own cloud file untouched", open(os.path.join(cloud, 'Game_own_file.json')).read() == 'the game auto-cloud file')
        check('status line logged', T.wait_log('cloud: Cloud backup: ', 20))
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
        check("owner's real save folder unchanged (hash before/after)", tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs + UserSetting identical (check_prefs)", r.check_prefs(restore=False))
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)   # any FAIL, guard violation or no check: exit 1
