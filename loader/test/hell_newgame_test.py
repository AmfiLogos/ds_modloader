#!/usr/bin/env python3
"""Player report 2026-10-20 (Windows): with hell-difficulty on, a NEW game with COMBAT difficulty Hell (5) stayed on the
loading screen; management Hell alone loaded. This test starts new games on a hidden Mac test copy (default W) with
hell-difficulty + multiplayer (for the mpm_newcampaign test command) and checks each reaches the world.
  hell_newgame_test.py [out] [combat:mgmt ...]     default cases 5:1 4:1 1:5 5:5
Exit 0 = every case loaded. Output: <out>/ (dsload + player logs per case, summary.json)."""
import json, os, shutil, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/hell_newgame')
CASES = [tuple(int(x) for x in c.split(':')) for c in sys.argv[2:]] or [(5, 1), (4, 1), (1, 5), (5, 5)]
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
X = os.environ.get('DSL_TEST_COPY', 'W')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
check, log = T.check, T.log


def run_case(cd, md):
    tag = f'c{cd}m{md}'
    sv = os.path.join(T.I, 'data', 'Saves')
    if os.path.isdir(sv):
        old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S') + '_' + tag); os.makedirs(old, exist_ok=True); shutil.move(sv, old)
    pid = T.start_a(enabled=('multiplayer', 'hell-difficulty'))
    ok = False
    try:
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', 300); time.sleep(3)
        check(f'{tag}: hell-difficulty ready', 'Hell tables ready' in T.dlog() and 'Hell difficulty ready' in T.dlog())
        log(tag, r.send(X, {'cmd': 'mpm_newcampaign', 'name': 'Hell ' + tag, 'combat': cd, 'mgmt': md, 'scenario': 0}, timeout=20, connect_wait=0))
        t0 = time.time(); last = {}
        while time.time() - t0 < 240:
            try:
                last = T.status()
                r.send(X, {'cmd': 'mpm_skipintro'}, timeout=10, connect_wait=0)
            except Exception as e:
                last = {'err': str(e)}
            if last.get('scene') == 2 and not last.get('busy') and not (last.get('map_loading') or {}).get('is_loading') and time.time() - t0 > 20:
                ok = True; break
            time.sleep(2)
        check(f'{tag}: new game reaches the world', ok, last)
        try: pl = open(os.path.join(T.I, 'player.log'), errors='replace').read()
        except OSError: pl = ''
        bad = [l for l in pl.splitlines() if 'Exception' in l or '[EnvHelper]' in l]
        check(f'{tag}: no exception / EnvHelper error in player.log', not bad, bad[:3])
        ok = ok and not bad
    finally:
        d = os.path.join(OUT, tag); os.makedirs(d, exist_ok=True)
        for f in ('dsload.log', 'player.log'):
            try: shutil.copy2(os.path.join(T.I, f), os.path.join(d, f))
            except OSError: pass
        try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        for p in T.game_pids():
            try: os.kill(p, 9)
            except OSError: pass
    return ok


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    threading.Thread(target=T.guard, daemon=True).start()
    res = {f'{c}:{m}': run_case(c, m) for c, m in CASES}
    T.stop_guard.set()
    json.dump({'cases': res, 'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    ok = all(res.values()) and not T.violations
    print('RESULT', 'PASS' if ok else 'FAIL', res)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
