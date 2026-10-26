#!/usr/bin/env python3
"""Scene-search rate of the loader and its mods on ONE hidden test copy (default FSC; test copies only, never the
owner's install, saves, prefs or screen; one copy at a time).

The real break this catches: a steady scene search (Object.FindObjectOfType / FindObjectsOfType / FindAnyObjectByType /
Resources.FindObjectsOfTypeAll) every few frames in an idle state. Each search walks every object of the scene (2-10 ms
on Mac, more on Windows and in big worlds): a slow frame every few frames = stutter for every player. The 2026-10-26 FPS
audit measured, with no mod at all: the loader's SaveLoadView poll 238 searches / min in every world, the title corner's
window check 1430 slow frames / min at the idle title (ds_multiplayer ext/fpsaudit/mac_measure.md).
The loader's scan guard (test copies only, the same gate as the kit's UI-RULE lines) counts every such call per calling
module and logs "SCAN-RULE <module> <n>/min <state>" when a module goes over its steady-state limit. This test FAILS on
any SCAN-RULE line, and when the guard is not active (no "scan guard: on" line).

Flow (loader/test/scanrate/scan_drv.c drives the copy by file commands, it hooks nothing): title idle 60 s; the Load
Campaign list open 30 s; list closed; the wiki's public base save under a fresh world id loaded, speed 1, world idle 60 s;
the world with one key press a second, 30 s. A player-like start in batchmode: DSRIG_LIVE=1, no control socket, the
loader's cloud backup against a FAKE Steam Cloud folder, crash report consent "no", no update check.

Usage: scan_rate_test.py <out> [--mods none|default] [--copy FSC] [--dsload PATH] [--mods-dir PATH]
  --mods none (default): the loader alone (an empty mods folder unless --mods-dir); default = multiplayer, custom-modes,
  gcd-cdr, live-hp, sound-fix from --mods-dir (default this tree's loader/build/mods). --dsload: default this tree's
  loader/build/dsload.dylib. Takes about 5 minutes. Lease: rig.py lease fpsscan (taken and given back here).
Last line: RESULT scan_rate PASS|FAIL <n SCAN-RULE lines> mods=<none|default>"""
import json, os, re, shutil, subprocess, sys, time, uuid
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r

a = sys.argv[1:]
if not a or a[0].startswith('-'): raise SystemExit(__doc__)
OUT = os.path.abspath(a[0])
opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
MODSET = opt('--mods', 'none')
if MODSET not in ('none', 'default'): raise SystemExit('--mods none|default')
X = opt('--copy', 'FSC'); I = r.idir(X)
LOADER = os.path.dirname(HERE)
DSLOAD = os.path.abspath(os.path.expanduser(opt('--dsload', os.path.join(LOADER, 'build', 'dsload.dylib'))))
MODS_DIR = opt('--mods-dir')
DEFAULT_SET = ['multiplayer', 'custom-modes', 'gcd-cdr', 'live-hp', 'sound-fix']
MODS = DEFAULT_SET if MODSET == 'default' else []
BASE = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public (the wiki's base save)
FAKE_SRC = os.path.join(HERE, 'cloud', 'fake_steam_api.c')
DRV_SRC = os.path.join(HERE, 'scanrate', 'scan_drv.c')
TITLE_S, LIST_S, WORLD_S, KEYS = 60, 30, 60, 30
PD = os.path.join(OUT, 'drv')
RULE = re.compile(r'SCAN-RULE (\S+) (\d+)/min (\S+)')
SUMMARY = re.compile(r'scan: (\S+) ([\d.]+) s:(.*)')
GUARD = re.compile(r'scan guard: (.*)')
log = lambda *x: print(time.strftime('%H:%M:%S'), X, *x, flush=True)
phases = []


def owner_playing():
    p = subprocess.run(['pgrep', '-f', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/|steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/MacOS/'],
                       capture_output=True, text=True).stdout.split()
    return [x for x in p if x]


def wait_load(limit=16.0):
    t0 = time.time()
    while os.getloadavg()[0] > limit:
        if time.time() - t0 > 1200: raise SystemExit(f'load stayed above {limit} for 20 min')
        log(f'waiting: 1-minute load {os.getloadavg()[0]:.1f} > {limit}'); time.sleep(30)


def build(src, out, cmd):
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        subprocess.run(cmd + ['-o', out, src], check=True)
    return out


def state():
    d = {}
    try:
        for l in open(os.path.join(PD, 'state.txt')):
            k, _, v = l.rstrip('\n').partition(' '); d[k] = v
    except OSError:
        pass
    return d


def wait_state(pred, what, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if not r.alive(r.read_pid(X)): raise RuntimeError(f'copy died waiting for {what}')
        s = state()
        if s and pred(s): return s
        time.sleep(0.5)
    raise RuntimeError(f'timeout waiting for {what}: {state()}')


def cmd(line, timeout=10):
    out = os.path.join(PD, 'cmd.out'); n0 = len(open(out).readlines()) if os.path.exists(out) else 0
    p = os.path.join(PD, 'cmd.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(out):
            ls = open(out).readlines()
            if len(ls) > n0: log('cmd', line, '->', ls[-1].strip()); return ls[-1].split()[1] == 'ok'
        time.sleep(0.2)
    log('cmd', line, '-> no answer'); return False


def phase(name, secs):
    t0 = time.time(); time.sleep(secs); phases.append({'name': name, 't0': t0, 't1': time.time()}); log('phase', name, 'done')


def world_save(path, guid, stem, clan):
    w = json.load(open(BASE)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


def parse(text):
    """dsload.log -> (SCAN-RULE lines, guard lines, [(state, secs, {module: n})])"""
    rules, guard, summ = [], [], []
    for l in text.splitlines():
        m = RULE.search(l)
        if m: rules.append(l.strip()); continue
        m = GUARD.search(l)
        if m: guard.append(m.group(1).strip()); continue
        m = SUMMARY.search(l)
        if m: summ.append((m.group(1), float(m.group(2)), {k: int(v) for k, v in re.findall(r'([\w.+-]+)[ =](\d+)', m.group(3))}))
    return rules, guard, summ


def report(text):
    """prints the guard lines, the per-state table and every SCAN-RULE line; returns (ok, n rule lines, why)"""
    rules, guard, summ = parse(text)
    for g in guard: print('guard:', g)
    if summ:
        mods = sorted({k for _, _, d in summ for k in d}, key=lambda k: (k != 'loader', k == 'game', k))
        print('per state (searches):')
        print('  %-8s %7s  ' % ('state', 'secs') + '  '.join('%12s' % m for m in mods))
        for st, secs, d in summ: print('  %-8s %7.1f  ' % (st, secs) + '  '.join('%12s' % d.get(m, 0) for m in mods))
    for l in rules: print('RULE', l)
    if not any(g.startswith('on') for g in guard): return False, len(rules), 'guard not active (no "scan guard: on" line)'
    return not rules, len(rules), f'{len(rules)} SCAN-RULE line(s)' if rules else 'no SCAN-RULE line'


def run():
    if owner_playing(): raise SystemExit('the owner\'s game runs: not starting a test copy')
    if not os.path.exists(os.path.join(I, r.EXE_REL)): raise SystemExit(f'no copy {X}: run loader/test/make_instance.sh {X}')
    if r.alive(r.read_pid(X)): raise SystemExit(f'{X} already runs')
    if not os.path.exists(DSLOAD): raise SystemExit(f'no loader at {DSLOAD} (zsh loader/build.sh, or --dsload)')
    wait_load()
    os.makedirs(PD, exist_ok=True)
    drv = build(DRV_SRC, os.path.join(OUT, 'scan_drv.dylib'), ['clang', '-arch', 'arm64', '-O2', '-fexceptions', '-dynamiclib', '-framework', 'AppKit', '-lobjc'])
    fake = build(FAKE_SRC, os.path.join(OUT, 'fake_steam_api.dylib'), ['clang', '-O0', '-dynamiclib'])
    # ---- data folders: the loader without multiplayer = $HOME-based; multiplayer = <copy>/data (its persistentDataPath hook)
    home_data = os.path.join(I, 'home', 'Library', 'Application Support', 'CanOpener', 'Dungeon Settlers')
    data = os.path.join(I, 'data') if 'multiplayer' in MODS else home_data
    aside = os.path.join(OUT, 'old_data'); os.makedirs(aside, exist_ok=True)
    for d in (os.path.join(I, 'data'), home_data):          # every run starts with no saves but its own world (moved, never deleted)
        for sub in ('Saves', 'Mods', 'SaveHeaderCache.json', 'fe', 'mp_owners'):
            q = os.path.join(d, sub)
            if os.path.exists(q): shutil.move(q, os.path.join(aside, (os.path.basename(d) + '_' + sub).replace(' ', '_') + time.strftime('_%H%M%S')))
    for f in ('state.txt', 'cmd.out', 'cmd.txt', 'cmd.taken', 'keys.txt'):
        q = os.path.join(PD, f)
        if os.path.exists(q): os.replace(q, q + '.prev')
    r.prepare_data(X)                                         # UserSetting.json: muted, VSync off, windowed 960x540 (<copy>/data)
    os.makedirs(os.path.join(home_data, 'Saves'), exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        s = os.path.join(I, 'data', f)
        if os.path.exists(s): shutil.copy2(s, os.path.join(home_data, f))
    guid = str(uuid.uuid4()); stem = 'ScanRate__AutoSave_' + guid.replace('-', '') + '_0'
    os.makedirs(os.path.join(data, 'Saves'), exist_ok=True)
    world_save(os.path.join(data, 'Saves', stem + '.json'), guid, stem, 'Scanrate')
    # ---- env (a player's game in batchmode; the scan guard is on because the copy is a background-only app)
    mods_dir = os.path.abspath(os.path.expanduser(MODS_DIR)) if MODS_DIR else (os.path.join(LOADER, 'build', 'mods') if MODS else os.path.join(OUT, 'no_mods'))
    os.makedirs(mods_dir, exist_ok=True)
    for m in MODS:
        if not os.path.isdir(os.path.join(mods_dir, m)): raise SystemExit(f'mod {m} not in {mods_dir} (zsh loader/build.sh, or --mods-dir)')
    root = os.path.join(OUT, 'root'); os.makedirs(os.path.join(root, 'crash'), exist_ok=True); os.makedirs(os.path.join(root, 'loader'), exist_ok=True)
    open(os.path.join(root, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')
    prof = os.path.join(OUT, 'profile.json'); json.dump({'enabled': MODS, 'order': MODS}, open(prof, 'w'))
    cloud = os.path.join(OUT, 'fake_cloud'); os.makedirs(cloud, exist_ok=True)
    env = {'SCAN_DRV_DIR': PD, 'SCAN_DRV_KEYS': '1', 'DSL_MODS_DIR': mods_dir, 'DSL_PROFILE': prof, 'DSML_ROOT': root, 'DSL_UPDATE': '0',
           'DSML_DSLOAD': DSLOAD, 'DSML_DATA': data, 'DSL_CLOUD_API': fake, 'FAKE_CLOUD_DIR': cloud, 'DSRIG_LIVE': '1'}
    for k in [k for k in os.environ if k.startswith(('DSL_', 'DSML_', 'DSRIG_', 'FAKE_', 'LRIG_'))]: os.environ.pop(k)
    os.environ['LRIG_DYLIB'] = DSLOAD; os.environ['LRIG_EXTRA_INSERT'] = drv
    orig = r.ctl_env
    r.ctl_env = lambda: {k: v for k, v in {**orig(), **env}.items() if k != 'DSRIG_CTL'}   # no control socket (a player's game)
    home = os.path.expanduser('~')
    json.dump({'copy': X, 'mods': MODS, 'dsload': DSLOAD.replace(home, '~'), 'env': {k: v.replace(home, '~') for k, v in env.items()},
               'save': stem, 'load1': os.getloadavg()[0]}, open(os.path.join(OUT, 'run.json'), 'w'), indent=1)
    t_start = time.time()
    try:
        r.start(X, fps=60)
    finally:
        r.ctl_env = orig
    steps = {}
    try:
        s = wait_state(lambda s: s.get('swapped') not in (None, '0'), 'pointer swap', 120)
        s = wait_state(lambda s: s.get('scene') == '1' and s.get('boot') == '1' and s.get('busy') == '0', 'title ready', 400)
        log('title ready', round(time.time() - t_start, 1), 's')
        time.sleep(5)
        phase('title', TITLE_S)
        steps['openload'] = cmd('openload')
        time.sleep(2); phase('list', LIST_S)
        steps['close_list'] = cmd('unpopup 5'); time.sleep(2)
        steps['load'] = cmd(f'load {stem}')
        wait_state(lambda s: s.get('scene') == '2' and s.get('busy') == '0' and int(s.get('tick', -1)) > 0, 'world loaded', 400)
        log('world loaded'); time.sleep(2)
        steps['speed'] = cmd('speed 1'); time.sleep(5)
        t1 = int(state().get('tick', 0)); phase('world', WORLD_S); steps['world_ticks'] = int(state().get('tick', 0)) - t1
        t0 = time.time(); steps['keys'] = cmd(f'keys {KEYS}'); time.sleep(KEYS + 1.5)
        phases.append({'name': 'world_keys', 't0': t0, 't1': time.time()}); log('phase world_keys done')
        try: steps['keys_sent'] = sum(1 for _ in open(os.path.join(PD, 'keys.txt')))
        except OSError: steps['keys_sent'] = 0
        st = state(); steps['ak_calls'], steps['ak_true'] = int(st.get('ak_calls', 0)), int(st.get('ak_true', 0))
    finally:
        json.dump(phases, open(os.path.join(OUT, 'phases.json'), 'w'), indent=1)
        r.stop(X)
        for f in ('player.log', 'dsload.log', 'guard.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(OUT, f))
        prefs = r.check_prefs()
        log('owner prefs', 'unchanged' if prefs else 'CHANGED')
    return steps


def main():
    os.makedirs(OUT, exist_ok=True)
    if '--parse' in a:                                        # self-check of the log reading: --parse <dsload.log>
        ok, n, why = report(open(opt('--parse'), errors='replace').read()); print(why); print(f'RESULT scan_rate {"PASS" if ok else "FAIL"} {n} mods={MODSET}'); sys.exit(0 if ok else 1)
    rig = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
    lp = subprocess.run([sys.executable, rig, 'lease', 'fpsscan', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = lp.stdout.strip()
    if lp.returncode or not pair: raise SystemExit('no lease: ' + (lp.stderr.strip() or 'no free pair'))
    log('lease', pair)
    try:
        steps = run()
    finally:
        subprocess.run([sys.executable, rig, 'release', pair, 'fpsscan'], capture_output=True)
    flow = steps.get('openload') and steps.get('close_list') and steps.get('load') and steps.get('world_ticks', 0) > 0 and steps.get('keys_sent', 0) >= KEYS - 5
    print('flow:', json.dumps(steps))
    ok, n, why = report(open(os.path.join(OUT, 'dsload.log'), errors='replace').read()) if os.path.exists(os.path.join(OUT, 'dsload.log')) else (False, 0, 'no dsload.log')
    if not flow: ok, why = False, why + '; FLOW INCOMPLETE ' + json.dumps(steps)
    json.dump({'ok': ok, 'why': why, 'rules': n, 'steps': steps, 'mods': MODSET}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    print(why)
    print(f'RESULT scan_rate {"PASS" if ok else "FAIL"} {n} mods={MODSET}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
