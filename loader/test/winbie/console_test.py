#!/usr/bin/env python3
"""TEST ONLY, Windows test VM (ds_multiplayer VMWARE.md rules; winbot agent, winrig.py): the starter's BepInEx step
(start/win/bie_win.c bie_prepare, through build/bie_fail_test.exe "prepare") turns BepInEx's console off in
BepInEx\\config\\BepInEx.cfg (owner rule: the BepInEx console window never shows), and keeps the log file.

The game is the ZET-U test COPY C:\\dsm\\zetubie\\game (plain be.788, never the Steam install), after a zetu_bie_win.py
run (its test data folder). The ZET-U plugin (it also turns the console off) is moved out for the run and back after;
its test probe stays: it keeps the game's saves and settings in C:\\dsm\\zetubie\\data, away from LocalLow.
A throwaway root C:\\dsm\\bieconsole\\root has one bepinex mod (mod.json only) and be.788 in its cache (no download).
The owner's LocalLow + Steam userdata\\<id>\\2798330 are hashed before and compared after (owner_backup.py).

  console_test.py [out]     the VM must already run (started visibly by the caller); build.sh first

Real breaks it catches: a player's start through the loader shows the BepInEx console window; the loader's edit turns
disk logging off or loses the player's other BepInEx settings; BepInEx.cfg missing the setting after the step."""
import json, os, re, sys, time
H = os.path.expanduser('~')
WB = os.environ.get('WINBOT_DIR') or f'{H}/ds_winbot/ds_multiplayer/win/winbot'
sys.path.insert(0, WB)
import winrig as W                                                   # noqa: E402
import owner_backup as OB                                            # noqa: E402
HERE = os.path.dirname(os.path.abspath(__file__))
BE788 = os.environ.get('ZETU_BE788_ZIP') or f'{H}/ds_zetubie/win/be788_win_x64.zip'
GAME = r'C:\dsm\zetubie\game'; WK = r'C:\dsm\bieconsole'; ROOT = WK + r'\root'; DATA = r'C:\dsm\zetubie\data'
CFG = GAME + r'\BepInEx\config\BepInEx.cfg'; BLOG = GAME + r'\BepInEx\LogOutput.log'
CONSOLE_HOSTS = ('conhost.exe', 'openconsole.exe', 'windowsterminal.exe')
checks = []; w = None; PID = None; BK = None


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def console_windows(base):
    hits = []
    for p in w.a('ps').get('procs', []):
        for v in p.get('windows', []):
            t = v.get('title', '')
            if 'BepInEx' in t or (p.get('name', '').lower() in CONSOLE_HOSTS and p.get('pid') not in base): hits.append(f"{p.get('name')} {p.get('pid')}: {t}")
    return hits
def sections(t):
    def sec(n):
        i = t.find('[' + n + ']'); j = t.find('\n[', i + 1)
        return re.findall(r'(?m)^Enabled = (\w+)', t[i:j if j > 0 else len(t)]) if i >= 0 else []
    return sec('Logging.Console'), sec('Logging.Disk')


def run(out):
    global PID, BK
    S = {}
    log('owner data backup'); BK = S['owner_backup'] = OB.backup(w)
    if not w.exists(GAME + r'\BepInEx\core\BepInEx.Unity.IL2CPP.dll'): raise SystemExit('no ZET-U test copy with be.788 in ' + GAME)
    park = WK + r'\old\run_' + time.strftime('%Y%m%d-%H%M%S'); S['park'] = park
    moved = []
    for n in ('ZetuSuite',):                                     # the plugin that also turns the console off: out
        p = GAME + r'\BepInEx\plugins' + '\\' + n
        if w.exists(p): w.move(p, park + '\\' + n); moved.append(n)
    S['moved'] = moved
    try:
        if w.exists(ROOT): w.move(ROOT, park + r'\root')
        w.write(ROOT + r'\mods\bie-console\mod.json', json.dumps({'id': 'bie-console', 'version': '1.0.0', 'kind': 'bepinex'}))
        w.upload(BE788, ROOT + r'\cache\BepInEx-be.788-win-x64.zip')
        w.put(os.path.join(HERE, 'build', 'bie_fail_test.exe'), WK + r'\bie_fail_test.exe')
        t = w.text(CFG) or ''                                    # the player's file: console on, a setting of theirs
        t = re.sub(r'(\[Logging\.Console\][^\[]*?)(?m:^Enabled = \w+)', r'\1Enabled = true', t, count=1)
        if '[Logging.Console]' not in t: t += '\n[Logging.Console]\n\nEnabled = true\n'
        S['before'] = sections(t); w.write(CFG, t)
        r = w.a('run', exe=WK + r'\bie_fail_test.exe', args=f'prepare "{ROOT}" "{GAME}"', cwd=WK, wait_ms=300000, capture=True)
        S['prepare'] = r.get('out', '')[-1500:]
        t2 = w.text(CFG) or ''; S['after'] = sections(t2)
        check('B: the starter step ran (BepInEx mod on)', 'prepare 0 any 1' in S['prepare'], S['prepare'])
        check('B: BepInEx.cfg: console off, disk log still on', S['before'][0] == ['true'] and S['after'] == (['false'], S['before'][1] or ['true'])
              and 'console window turned off' in S['prepare'], [S['before'], S['after']])
        check('B: every other line of the player\'s file kept', t2.replace('Enabled = false', 'Enabled = true', 1).replace('\r\n', '\n') == t.replace('\r\n', '\n'),
              len(t2) - len(t))
        # the game started as the starter starts it (Doorstop on): no console window, at any time; the log file written
        if not w.screen_mode_ok(): raise SystemExit('screen mode is not windowed (3): not started')
        if w.procs(): raise SystemExit('another DungeonSettlers.exe runs in the VM (another lane): not started')
        if w.exists(BLOG): w.move(BLOG, park + r'\LogOutput.log')
        base = {p.get('pid') for p in w.a('ps').get('procs', []) if p.get('name', '').lower() in CONSOLE_HOSTS}
        env = [f'ZETU_TEST_DATA={DATA}', r'DSRIG_DIR=C:\dsm\zetubie', 'SteamAppId=2798330', 'SteamGameId=2798330']
        PID = w.a('run', exe=GAME + r'\DungeonSettlers.exe', args=f'-logFile "{WK}\\player.log"', cwd=GAME, env=env, show=True, wait_ms=0).get('pid')
        log('game pid', PID)
        seen, t0, done = [], time.time(), None
        while time.time() - t0 < 600:
            for h in console_windows(base):
                if h not in seen: seen.append(h); log('console window:', h)
            L = w.text(BLOG) or ''
            if done is None and 'Chainloader startup complete' in L: done = time.time()
            if done and time.time() - done > 10: break
            if not w.procs(): break
            time.sleep(0.3)
        L = w.text(BLOG) or ''; S['seen'] = seen; S['log_lines'] = L.count('\n')
        w.get(BLOG, os.path.join(out, 'LogOutput.log'))
        check('B: the game with BepInEx started (chainloader done)', done is not None, L[-300:])
        check('B: no BepInEx console window at any time of the start', done is not None and not seen, seen)
        check('B: LogOutput.log written', S['log_lines'] > 10, S['log_lines'])
    finally:
        if PID: w.a('kill', pid=PID); PID = None; time.sleep(3)
        p = GAME + r'\BepInEx\plugins\bie-console'
        if w.exists(p): w.move(p, park + r'\plugins_bie-console')
        for n in moved: w.move(park + '\\' + n, GAME + r'\BepInEx\plugins' + '\\' + n)
    return S


def main():
    global w
    out = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_bieconsole/out/win')
    os.makedirs(out, exist_ok=True)
    w = W.Win(); w.up()
    S = {}
    try:
        S = run(out)
    except BaseException as e:
        import traceback; traceback.print_exc()
        check('run finished', False, repr(e))
    finally:
        if BK:
            c = OB.compare(w, BK); S['owner_compare'] = c
            check('O: the owner\'s LocalLow + Steam userdata files unchanged, no pending cloud upload', not c['diff'] and not c['remotecache_syncstate3'], c)
        S['checks'] = checks; json.dump(S, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    print(f'\n{len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
