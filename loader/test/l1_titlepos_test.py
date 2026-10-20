#!/usr/bin/env python3
"""Title column: the loader's "Mods" button is in the same place whatever runs (owner 2026-10-20).
Right below the stock "Load Campaign" button's block: Load Campaign, [Multiplayer + its clones], Mods,
[Units Laboratory], ... With the old rule ("without Multiplayer, last in the column") the cases without multiplayer fail.

Four starts of one hidden test copy (default R; DSL_TP_X=<letter>), mods of THIS worktree's build/:
  all on | multiplayer off + Units Laboratory on | both off | multiplayer on + Units Laboratory off.
Take a lease first (rig.py lease modspos --pid <this pid>) or let the script take one. Output: <out>/ (shots, summary.json)."""
import glob, json, os, re, shutil, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import l1_test as T
r = T.r
X = os.environ.get('DSL_TP_X', 'R')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
I = T.I
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/titlepos')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT
BUILD = os.path.join(os.path.dirname(HERE), 'build')
log, check = T.log, T.check
CASES = [('all_on', ('multiplayer', 'custom-modes')), ('mp_off_ul_on', ('custom-modes',)),
         ('both_off', ()), ('mp_on_ul_off', ('multiplayer',))]


def install():
    app = os.path.join(I, 'DungeonSettlers.app')
    T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    for m in ('multiplayer', 'custom-modes'):
        d = os.path.join(BUILD, 'mods', m)
        for f in glob.glob(os.path.join(d, '**', '*'), recursive=True):
            if os.path.isfile(f):
                t = os.path.join(md, m, os.path.relpath(f, d)); os.makedirs(os.path.dirname(t), exist_ok=True)
                shutil.copy2(f, t + '.tmp'); os.replace(t + '.tmp', t)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def seed_windowed():
    """without MP the game's persistentDataPath is the copy's own home folder: windowed 960x540 there too (uikit trap)"""
    r.prepare_data(X)
    pd = os.path.join(I, 'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mp' + X); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    g = json.load(open(os.path.join(pd, 'UserSetting.json')))['GraphicSettingData']
    assert g['SaveCurrentScreenType'] == 1 and g['ResolutionWidth'] == 960, g


def stop_all():
    for p in T.game_pids():
        try: os.kill(p, 15)
        except OSError: pass
    t0 = time.time()
    while T.game_pids() and time.time() - t0 < 15: time.sleep(0.3)
    for p in T.game_pids():
        try: os.kill(p, 9)
        except OSError: pass


def title_labels():
    mark = len(T.dlog()); T.request({'cmd': 'title'})
    t0 = time.time()
    while time.time() - t0 < 10:
        m = re.search(r'reg test: title (.*)', T.dlog()[mark:])
        if m:
            items = [x.split('|') for x in m.group(1).split(';') if x]
            return [x[0] for x in items if len(x) > 1 and x[1] == '1']
        time.sleep(0.2)
    return None


def run_case(name, mods):
    seed_windowed()
    cr = os.path.join(I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')
    T.start_a(enabled=mods, worlds={}, extra_env={'DSL_CRASHREP_TEST': cr})
    ok = T.wait_log('ui: title button "Mods" added to the title column', 180)
    if ok and 'multiplayer' in mods:                              # the clone exists under the splash: wait for the title itself
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', 120)
    time.sleep(4)                                                 # the multiplayer / Units Laboratory clones, the menu fit
    shot = os.path.join(OUT, 'title_' + name + '.png')
    for _ in range(12):                                           # the splash (a small flat frame) can still cover the title
        if os.path.exists(shot): os.replace(shot, shot + '.splash')
        T.shot('title_' + name)
        if os.path.exists(shot) and os.path.getsize(shot) > 60000: break
        time.sleep(3)
    labels = title_labels() if ok else None
    stop_all()
    return labels


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'copy': X, 'violations': T.violations, 'checks': T.checks, 'cases': {}}
    try:
        for name, mods in CASES:
            L = run_case(name, mods); S['cases'][name] = L
            log(name, L)
            if not L or 'Mods' not in L:
                check(f'{name}: title read', False, L); continue
            want = 'Multiplayer' if 'multiplayer' in mods else None
            load = next((i for i, l in enumerate(L) if l.lower().startswith('load')), -1)
            im = L.index('Mods')
            if want:
                check(f'{name}: Mods right below Multiplayer', want in L and im == L.index(want) + 1 and L.index(want) == load + 1, L)
            else:
                check(f'{name}: Mods right below Load Campaign', load >= 0 and im == load + 1, L)
            if 'custom-modes' in mods:
                check(f'{name}: Units Laboratory right below Mods', 'Units Laboratory' in L and L.index('Units Laboratory') == im + 1, L)
    finally:
        stop_all(); T.stop_guard.set()
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    bad = [c for c in T.checks if not c['ok']]
    log('RESULT', 'PASS' if not bad and not T.violations else 'FAIL', f'{len(T.checks) - len(bad)}/{len(T.checks)}')
    sys.exit(1 if bad or T.violations else 0)


if __name__ == '__main__':
    main()
