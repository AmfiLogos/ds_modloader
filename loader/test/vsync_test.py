#!/usr/bin/env python3
"""The boot speed-up never changes the player's VSync or frame cap (FPS audit 2026-10-26, row 1; loader dsl_soft.c
force_fps / fast_end). The real break it catches: with Settings > VSync OFF, v0.1.28 left vSyncCount 1 for the whole
session after the boot speed-up (the first value it read was Unity's default, before the game applied the setting); on a
60 Hz screen every frame that misses 16.7 ms then waits a whole refresh. VSync ON must stay 1.

One hidden test copy (default Y; DSL_TEST_COPY), batchmode, one rig lease, no multiplayer module (its rig mode sets the
frame cap and vsync itself). Never the owner's game, saves, prefs or screen. vsync-probe (test/vsync_probe.c, test only)
logs vSyncCount, targetFrameRate, the scene and the player's VSync setting each time one changes, and opens the world
(the wiki's public base save under its own name, in the copy's own folders).
Runs (each: start, title, check, open the world, world, check, stop):
  base  VSync OFF, boot speed-up OFF (DSL_FB_SPLASH/FPS/BLP=0): the game's own values (its frame cap, vsync 0)
  off   VSync OFF, boot speed-up on (the default): vsync 0 and the game's frame cap, at the title and in the world
  on    VSync ON,  boot speed-up on: vsync 1 and the game's frame cap, at the title and in the world
  vsync_test.py [out]     RESULT line + <out>/summary.json; exit 1 on any FAIL
  VSYNC_TEST_DYLIB=<dsload.dylib>   another loader build (e.g. main's, to show the bug); default this tree's build
"""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('DSL_TEST_COPY', 'Y'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
MAP = os.path.join(H, 'ds_multiplayer', 'maps', 'base_25846389.json')
SAVE = 'Template__AutoSave_491bc26098aa4cd1b63f82da78369aaf_0'      # the base map's world
HOME_DATA = os.path.join(I, f'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mp{X}')   # persistentDataPath without MP
OWNER_GAME = 'steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/MacOS/|DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/'
DYL = os.path.expanduser(os.environ.get('VSYNC_TEST_DYLIB') or os.path.join(LB, 'build', 'dsload.dylib'))
FB_OFF = {'DSL_FB_SPLASH': '0', 'DSL_FB_FPS': '0', 'DSL_FB_BLP': '0'}
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def aside(p, out):
    if os.path.lexists(p): os.replace(p, os.path.join(out, 'aside_' + os.path.basename(p) + '_%d' % time.time_ns()))


LINE = re.compile(r'vsync-probe: (?:state )?frame (\d+)(?: t [\d.]+)? scene (-?\d+) boot (-?\d+) vsync (-?\d+) fps (-?\d+) setting (-?\d+)')
def probe_lines(mark=0): return [dict(zip(('frame', 'scene', 'boot', 'vsync', 'fps', 'setting'), map(int, m.groups()))) for m in LINE.finditer(dlog()[mark:])]


def wait(pred, what, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        if not r.alive(r.read_pid(X)): raise RuntimeError(f'the copy ended while waiting for {what}')
        time.sleep(0.5)
    raise RuntimeError(f'timeout waiting for {what}')


def request(line):
    p = os.path.join(I, 'vsync_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)


def state():
    """the values now (the probe's 'state' answer)"""
    mark = len(dlog()); request('state')
    s = wait(lambda: [l for l in dlog()[mark:].splitlines() if 'vsync-probe: state' in l], 'probe state', 30)[-1]
    return dict(zip(('frame', 'scene', 'boot', 'vsync', 'fps', 'setting'), map(int, LINE.search(s).groups())))


def build_probe(md):
    pd = os.path.join(md, 'vsync-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/vsync-probe.dylib',
                    '-o', os.path.join(pd, 'vsync-probe.dylib'), os.path.join(HERE, 'vsync_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'vsync-probe.dylib')], capture_output=True)
    json.dump({'id': 'vsync-probe', 'name': 'vsync probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'vsync-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)


def run(tag, vsync_on, speedup, out):
    """one start of the copy: values at the title (after the boot speed-up ended) and in the world"""
    rd = os.path.join(out, tag); os.makedirs(rd)
    if subprocess.run(['pgrep', '-f', OWNER_GAME], capture_output=True).returncode == 0:
        raise SystemExit("the owner's game runs: no test copy starts now")
    for p in ('dsload.log', 'player.log', 'vsync_probe_req.txt', 'softrestart'):
        aside(os.path.join(I, p), rd)
    r.prepare_data(X)
    os.makedirs(os.path.join(HOME_DATA, 'Saves'), exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):                     # the copy's own windowed settings (uikit trap), then this run's VSync
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(HOME_DATA, f))
    for us in (os.path.join(I, 'data', 'UserSetting.json'), os.path.join(HOME_DATA, 'UserSetting.json')):
        s = json.load(open(us)); s['GraphicSettingData']['VSyncEnabled'] = vsync_on; json.dump(s, open(us, 'w'), indent=1)
    g = json.load(open(os.path.join(HOME_DATA, 'UserSetting.json')))['GraphicSettingData']
    assert g['SaveCurrentScreenType'] == 1 and g['ResolutionWidth'] == 960 and g['VSyncEnabled'] == vsync_on, g
    for sd in (os.path.join(HOME_DATA, 'Saves'), os.path.join(I, 'data', 'Saves')):
        os.makedirs(sd, exist_ok=True); shutil.copy2(MAP, os.path.join(sd, SAVE + '.json'))
    for k in FB_OFF: os.environ.pop(k, None)
    if not speedup: os.environ.update(FB_OFF)
    r.start(X)
    try:
        wait(lambda: 'vsync-probe: ready' in dlog(), 'probe ready', 300)
        wait(lambda: any(l['scene'] == 1 and l['boot'] == 1 for l in probe_lines()), 'title', 600)
        if speedup: wait(lambda: 'A: boot done (' in dlog(), 'boot speed-up end', 120)
        time.sleep(2)                                                     # a few seconds of the idle title
        t = state(); log(tag, 'title', t)
        mark = len(dlog()); request('load ' + SAVE)
        wait(lambda: 'LoadCampaignRequested' in dlog()[mark:], 'load queued', 30)
        wait(lambda: any(l['scene'] == 2 for l in probe_lines(mark)), 'world scene', 300)
        time.sleep(8)                                                     # the world loads and runs a few seconds
        w = state(); log(tag, 'world', w)
        hist = probe_lines()
    finally:
        pid = r.read_pid(X)
        if r.alive(pid):                                                  # the game's own quit (no socket without MP), else lrig's stop
            request('quit'); t1 = time.time()
            while r.alive(pid) and time.time() - t1 < 20: time.sleep(0.5)
        try: r.stop(X)
        except Exception as e: log('stop:', e)
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), rd)
    open(os.path.join(rd, 'probe.json'), 'w').write(json.dumps(hist, indent=0))
    return t, w, hist


def main():
    out = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else f'~/ds_launcher/loader/test_env/vsync_out/run_{time.strftime("%Y%m%d_%H%M%S")}'))
    os.makedirs(out, exist_ok=True)
    assert os.path.exists(DYL), 'build the loader first (loader/build.sh)'
    if r.alive(r.read_pid(X)): raise SystemExit(f'copy {X} already running')
    lane = 'vsync-%d' % os.getpid(); pair = None; t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): pair = p.stdout.strip(); break
        time.sleep(20)
    if not pair: raise SystemExit('no rig lease in 30 min')
    log('lease', pair, 'copy', X, 'loader', DYL, 'out', out)
    md = os.path.join(out, 'mods'); os.makedirs(md); build_probe(md)
    prof = os.path.join(out, 'profile.json'); json.dump({'enabled': ['vsync-probe'], 'order': ['vsync-probe']}, open(prof, 'w'))
    cr = os.path.join(out, 'crash'); os.makedirs(cr); open(os.path.join(cr, 'answer'), 'w').write('no')
    os.environ.update({'LRIG_DYLIB': DYL, 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_NO_ALERT': '1', 'DSL_UPDATE': '0',
                       'DSL_CRASHREP_TEST': cr})
    for k in ('DSML_SESSION', 'DSL_SOFT_TEST', 'DSRIG_LIVE'): os.environ.pop(k, None)
    res = {}
    try:
        for tag, von, sp in (('base', False, False), ('off', False, True), ('on', True, True)):
            try:
                res[tag] = run(tag, von, sp, out)
            except Exception as e:
                check(f'{tag}: run finished', False, e); res[tag] = None
        b = res.get('base')
        game_fps = b[0]['fps'] if b else None
        if b:
            check('base: the game sets vsync 0 itself with VSync OFF (setup)', b[0]['vsync'] == 0 and b[0]['setting'] == 0, b[0])
            check('base: the game keeps its own frame cap in the world', b[1]['fps'] == game_fps, (b[0], b[1]))
        for tag, want in (('off', 0), ('on', 1)):
            v = res.get(tag)
            if not v: continue
            t, w, hist = v
            check(f'{tag}: the copy has VSync {"ON" if want else "OFF"} in its settings', t['setting'] == want and w['setting'] == want, (t, w))
            check(f'{tag}: boot speed-up ran (frames uncapped during the boot)', any(l['fps'] == 10000 for l in hist), [l for l in hist[:6]])
            check(f'{tag}: title: vSyncCount {want} (the player\'s setting)', t['vsync'] == want, t)
            check(f'{tag}: world: vSyncCount {want} (the player\'s setting)', w['vsync'] == want, w)
            if game_fps is not None:
                check(f'{tag}: title: frame cap {game_fps} (the game\'s own)', t['fps'] == game_fps, t)
                check(f'{tag}: world: frame cap {game_fps} (the game\'s own)', w['fps'] == game_fps, w)
    finally:
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    nf = sum(1 for c in checks if not c['ok'])
    json.dump({'checks': checks, 'copy': X}, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    print(f'RESULT vsync_test: {len(checks) - nf} PASS, {nf} FAIL ({out})', flush=True)
    sys.exit(1 if nf or not checks else 0)


if __name__ == '__main__':
    main()
