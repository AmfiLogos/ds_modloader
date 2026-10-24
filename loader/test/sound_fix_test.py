#!/usr/bin/env python3
"""Sound settings fix (mod sound-fix): the Options window's sound slider changes the mixer the game's sounds play through,
at the title screen and in a world, on one hidden test copy (default U; DSL_TEST_COPY). One rig lease. Never the owner's
game, saves or prefs: the world is a copy of ds_multiplayer maps/base_<build>.json (public base map) in <out>.

  sound_fix_test.py [stock|mod|both] [out]     stock = without the mod (shows the game's bug), mod = with it; default both
  sound_fix_test.py probe '<line>'             sound-probe request to a running copy (state | open | slider g v | mute g 0/1 | uisfx)
The same flow (flow()) runs on the Windows VM: test/win/sound_fix_vm.py swaps the transport T (request file, dsload.log,
control commands, world load) for winrig's.

The real break the mod run catches: a slider or mute change in the Options window does not reach the game's mixer
(the player hears no change until a save loads), at the title or in a world. sound-probe (test/sound_probe.c, test only)
moves the window's slider the player's way (SubUI_Slider.SetValue -> UnityEngine.UI.Slider.onValueChanged) and reads the
game's mixer (SoundManager._audioMixer) and the stored setting back. The stock run must show the bug (its checks are
the game's behaviour, expected to differ), else the test proves nothing."""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('DSL_TEST_COPY', 'U'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
MAP = os.path.join(H, 'ds_multiplayer', 'maps', 'base_25846389.json')
GROUPS = ['Master', 'BGM', 'Ambience_BGM', 'SFX', 'UI_SFX']
checks = []
EVID = {}


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
class MacT:
    """transport of the Mac copy X: probe request file, its dsload.log, a world load through the rig socket"""
    def dlog(self):
        try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
        except OSError: return ''
    def req(self, line):
        p = os.path.join(I, 'sound_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    def load_world(self, out):
        src = os.path.join(out, 'save_in', os.path.basename(MAP)); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(MAP, src)
        log('load', r.send(X, {'cmd': 'load_save', 'file': src}, timeout=60, connect_wait=5))
        r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
T = MacT()
def dlog(): return T.dlog()
def aside(p, out):
    """never delete: a file / folder of the copy goes to <out>/aside/"""
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))


def probe(line, end, limit=20):
    """one request; the dsload.log lines 'sound-probe: ...' it wrote, up to the line that contains `end`"""
    mark = len(dlog())
    T.req(line)
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l.split('sound-probe: ', 1)[1] for l in dlog()[mark:].splitlines() if 'sound-probe: ' in l]
        if any(end in l for l in L): return L
        time.sleep(0.3)
    return [l.split('sound-probe: ', 1)[1] for l in dlog()[mark:].splitlines() if 'sound-probe: ' in l]


def vols(line):
    return {k: float(v) for k, v in re.findall(r'(\w+)=(-?[\d.]+)', line)}


def state():
    """{'game': {group: dB}, 'stored': {group: dB}, 'window': {...} or None, 'same': bool or None}"""
    L = probe('state', 'state done')
    S = {'game': {}, 'stored': {}, 'window': None, 'same': None, 'raw': L}
    for l in L:
        if l.startswith('state: game mixer'): S['game'] = vols(l.split(':', 2)[2])
        elif l.startswith('state: stored:'): S['stored'] = vols(l.split('stored:', 1)[1])
        elif l.startswith('state: window mixer IS'): S['same'] = True
        elif l.startswith('state: window mixer is NOT'): S['same'] = False
        elif l.startswith('state: window mixer'): S['window'] = vols(l.split(':', 2)[2])
    log('state game', S['game'], 'stored', S['stored'], 'window', S['window'], 'same', S['same'])
    return S


def near(a, b): return a is not None and b is not None and abs(a - b) < 0.05


def lease():
    lane = 'soundfix-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def up(out, with_mod):
    """multiplayer (rig control) + sound-probe (+ sound-fix) from this tree's build; copy X started hidden, at the title"""
    if r.alive(r.read_pid(X)): raise SystemExit(f'copy {X} already running')
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    for m in ('multiplayer',) + (('sound-fix',) if with_mod else ()):        # the shipped files when build_mods.sh ran
        rel = os.path.join(LB, 'build', 'mods_all', 'mac', m)
        shutil.copytree(rel if os.path.isdir(rel) else os.path.join(LB, 'build', 'mods', m), os.path.join(md, m))
    pd = os.path.join(md, 'sound-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/sound-probe.dylib',
                    '-o', os.path.join(pd, 'sound-probe.dylib'), os.path.join(HERE, 'sound_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'sound-probe.dylib')], capture_output=True)
    json.dump({'id': 'sound-probe', 'name': 'sound probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'sound-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
    en = ['multiplayer', 'sound-probe'] + (['sound-fix'] if with_mod else [])
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': en, 'order': en}, open(prof, 'w'))
    for p in ('mp.json', 'dsload.log', 'player.log', 'sound_probe_req.txt', os.path.join('data', 'Saves'), os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    gp = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gp) if os.path.exists(gp) else 0
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_NO_ALERT': '1', 'DSL_UPDATE': '0'})
    os.environ.pop('DSML_SESSION', None)
    r.start(X, window=os.environ.get('SOUND_TEST_WINDOW') == '1')
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    time.sleep(3)
    return g0


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def open_window():
    log('open', probe('open', 'open:'))
    t0 = time.time()
    while time.time() - t0 < 30:
        S = state()
        if S['window'] is not None: return S
        time.sleep(1)
    return S


def slider_case(tag, g, v, with_mod, S0):
    """move group g's slider to v; with the mod the game's mixer must follow the stored value at once"""
    log('slider', probe(f'slider {g} {v}', 'slider'))
    name = GROUPS[g]; t0 = time.time()
    while True:                                        # the stored value changes when the game's event queue runs (next frames)
        S = state()
        if not near(S['stored'].get(name), S0['stored'].get(name)) or time.time() - t0 > 10: break
        time.sleep(0.5)
    st, gm = S['stored'].get(name), S['game'].get(name)
    check(f'{tag}: {name} slider {v}: the game stored it (stored {st} dB, was {S0["stored"].get(name)} dB)',
          st is not None and st > -80 and not near(st, S0['stored'].get(name)), S['raw'])
    if with_mod:
        check(f'{tag}: {name} slider {v}: the mixer the sounds play through follows at once (game {gm} dB = stored {st} dB)', near(gm, st), S['raw'])
    else:
        check(f'{tag}: STOCK BUG shown: {name} slider {v} leaves the game mixer as it was (game {gm} dB, before {S0["game"].get(name)} dB, stored {st} dB)',
              near(gm, S0['game'].get(name)) and not near(gm, st), S['raw'])
    return S


def run_one(kind, out):
    o = os.path.join(out, kind); os.makedirs(o, exist_ok=True)
    g0 = up(o, kind == 'mod')
    flow(kind, o)
    return g0


def flow(kind, o):
    """the copy is at the title with sound-probe (+ sound-fix for kind 'mod'): title, then a world"""
    with_mod = kind == 'mod'
    L = dlog()
    check(f'{kind}: probe ready', 'sound-probe: ready' in L)
    if with_mod:
        check('mod: sound-fix hooks in', 'sound sliders and mute buttons apply at once' in L, [l for l in L.splitlines() if 'sound-fix' in l][-5:])
    S0 = state()                                       # the game itself applies the saved volumes when the title loads
    log(f'{kind}: title, at start: game mixer', S0['game'], 'stored', S0['stored'])
    S1 = open_window()
    check(f'{kind}: Options window open', S1['window'] is not None, S1['raw'])
    check(f'{kind}: the window writes its OWN mixer (not the one the sounds play through): the cause', S1['same'] is False, S1['raw'])
    slider_case(f'{kind} title', 1, 37, with_mod, S1)
    if with_mod:
        log('mute', probe('mute 1 1', 'mute')); time.sleep(1.5); S = state()
        check('mod title: BGM Mute on: game mixer BGM -80 dB', near(S['game'].get('BGM'), -80.0), S['raw'])
        log('mute', probe('mute 1 0', 'mute')); time.sleep(1.5); S = state()
        check('mod title: BGM Mute off: game mixer BGM back to the slider value', near(S['game'].get('BGM'), S['stored'].get('BGM')) and S['game'].get('BGM', -80) > -80, S['raw'])
    # ---- a world: the public base map, then the same slider check (Ambience)
    T.load_world(o)
    time.sleep(3)
    W0 = state()
    if with_mod:                                       # stock: the world load applies nothing (only the title does)
        check('mod: world loaded: the game mixer still has the stored volumes',
              all(near(W0['game'].get(n), W0['stored'].get(n)) for n in GROUPS), W0['raw'])
    EVID.setdefault(kind, {})['world_load'] = W0['raw']
    W1 = open_window()
    check(f'{kind}: world: Options window open', W1['window'] is not None, W1['raw'])
    slider_case(f'{kind} world', 2, 55, with_mod, W1)
    U = probe('uisfx', 'uisfx done')                  # evidence only (no mod code): the group the game gives a UI sound
    log('uisfx', U); EVID.setdefault(kind, {})['uisfx'] = U


def main():
    a = sys.argv[1:] or ['both']
    if a[0] == 'probe': print(probe(a[1], a[1].split()[0], 20)); return
    kinds = {'stock': ['stock'], 'mod': ['mod'], 'both': ['stock', 'mod']}[a[0]]
    out = os.path.expanduser(a[1] if len(a) > 1 else f'~/ds_launcher/loader/test_env/l1_out/sound_fix/{time.strftime("%Y%m%d-%H%M%S")}')
    os.makedirs(out, exist_ok=True)
    for f in ('dsload.dylib',): assert os.path.exists(os.path.join(LB, 'build', f)), 'build the loader first (loader/build.sh)'
    t0 = time.time()
    while True:                                        # the test copy cap first (no pair held while waiting), 90 min at most
        try: r.cap_wait('sound fix'); break
        except SystemExit:
            if time.time() - t0 > 5400: raise
    lane, pair = lease(); log('lease', pair)
    try:
        for k in kinds:
            g0 = 0
            try:
                g0 = run_one(k, out)
            except Exception as e:
                check(f'{k}: run finished', False, repr(e))
            finally:
                down()
                for f in ('dsload.log', 'player.log', 'guard.log'):
                    if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(out, k, f))
                g = open(os.path.join(I, 'guard.log'), errors='replace').read()[g0:] if os.path.exists(os.path.join(I, 'guard.log')) else ''
                check(f'{k}: guard: never a visible window, never frontmost', 'GUARD KILLED' not in g, g[-300:])
    finally:
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
        json.dump({'checks': checks, 'evidence': EVID}, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    for c in checks: print(('PASS ' if c['ok'] else 'FAIL ') + c['name'])
    print(f'\nRESULT {len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
