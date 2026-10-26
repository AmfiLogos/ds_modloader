#!/usr/bin/env python3
"""Start-up time: stock game vs a patched GameAssembly file vs the loader (no mods / default mods) vs BepInEx / MelonLoader
under the loader. Mac, one hidden test copy (never the owner's game, saves, prefs or screen). Measure only.

  boot_compare.py <out> [--runs N] [--cases a,b,c,c0,d,e,f]

Copy: BT (loader/test/make_instance.sh body with X=BT: APFS clone, stock GameAssembly + the Steam-gate instruction that every
test copy needs, own bundle id, LSBackgroundOnly). Started through lrig.py (guard process, hidden window, muted, 960x540).
Cases:
  a   stock: no loader. boot_probe -DHIDE keeps the window off the screen (hooks nothing in GameAssembly)
  b   "replace files": GameAssembly.dylib with Hell's 20 instruction patches written INTO the file (apply_hell.py way), no loader
  c0  loader, no mods, its boot speed-up off (DSL_FASTBOOT=0, DSL_PREWARM=0)
  c   loader, no mods
  d   loader, the default mods (multiplayer, custom-modes, gcd-cdr)
  e   d + a BepInEx mod (dsmp-bridge), our arm64 BepInEx: a FRESH BepInEx folder per round (first start), then a start on it
  f   d + a MelonLoader mod (hello-melon), our arm64 MelonLoader: the same first / later split
Marker (every case): boot_probe.c (inserted in every case) writes the process start time (kernel) and the time when
MainFlow._currentScene = 1 (Title) and MainFlow._bootReady = 1. Loader cases also: dsload.log "boot done (title ready)".
Results: <out>/results.jsonl + per-run logs. docs/MODDING.md "Startup time, measured" has the 2026-10-20 numbers.
Inputs (read only): LD = a loader build folder (dsload.dylib + mods_all/mac/<id>),
BIE_ZIP / ML_ZIP = the release zips the app downloads (sha256 checked by its install scripts)."""
import json, os, re, shutil, struct, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import lrig as r

X = 'BT'; I = r.idir(X)
APP = os.path.join(I, 'DungeonSettlers.app'); FW = os.path.join(APP, 'Contents', 'Frameworks')
HERE = os.path.dirname(os.path.abspath(__file__)); LOADER = os.path.dirname(os.path.dirname(HERE)); REPO = os.path.dirname(LOADER)
OUT = os.path.abspath(sys.argv[1]); os.makedirs(OUT, exist_ok=True)
a = sys.argv[2:]
RUNS = int(a[a.index('--runs') + 1]) if '--runs' in a else 3
CASES = (a[a.index('--cases') + 1] if '--cases' in a else 'a,b,c0,c,d,e,f').split(',')
LD = os.path.expanduser(os.environ.get('LD', os.path.join(LOADER, 'build')))
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
DEFAULT = ['multiplayer', 'custom-modes', 'gcd-cdr']
log = lambda *x: print(time.strftime('%H:%M:%S'), *x, flush=True)

# ---- frozen inputs (copied once into OUT, so a rebuild of LD during the runs changes nothing)
FZ = os.path.join(OUT, 'inputs'); os.makedirs(FZ, exist_ok=True)
DSLOAD = os.path.join(FZ, 'dsload.dylib'); MODS = os.path.join(FZ, 'mods')
if not os.path.exists(DSLOAD): shutil.copy2(os.path.join(LD, 'dsload.dylib'), DSLOAD)
for m in DEFAULT + ['dsmp-bridge']:
    if not os.path.exists(os.path.join(MODS, m)): shutil.copytree(os.path.join(LD, 'mods_all', 'mac', m), os.path.join(MODS, m))
HM = os.path.expanduser('~/ds_multiplayer/ext/melon/plugin/mods/hello-melon')
if 'f' in CASES and not os.path.exists(os.path.join(MODS, 'hello-melon')): shutil.copytree(HM, os.path.join(MODS, 'hello-melon'))
PROBES = {}
for v, flags in (('hide', ['-DHIDE']), ('title', [])):         # hide = stock cases (no dsload to hide the window); title = loader cases
    PROBES[v] = os.path.join(FZ, f'probe_{v}.dylib')
    if not os.path.exists(PROBES[v]):
        subprocess.run(['nice', '-n', '19', 'clang', '-arch', 'arm64', '-O2', *flags, '-dynamiclib', '-framework', 'AppKit', '-framework', 'CoreFoundation',
                        '-lobjc', '-o', PROBES[v], os.path.join(HERE, 'boot_probe.c')], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', PROBES[v]], check=True, capture_output=True)

# ---- the two GameAssembly files (swapped with mv between runs; the copy is not running then)
GA = os.path.join(FW, 'GameAssembly.dylib'); GA_GATE = os.path.join(I, 'ga_gate.dylib'); GA_HELL = os.path.join(I, 'ga_hellpatch.dylib')
if not os.path.exists(GA_GATE): subprocess.run(['cp', '-c', GA, GA_GATE], check=True)
if not os.path.exists(GA_HELL):
    b = bytearray(open(GA_GATE, 'rb').read()); n = struct.unpack('>I', b[4:8])[0]; base = None
    for i in range(n):
        ct, cs, off, size, al = struct.unpack('>iiIII', b[8 + 20 * i:28 + 20 * i])
        if ct == 0x0100000C: base = off
    k = 0
    for l in open(os.path.join(LOADER, 'modules', 'hell-difficulty', 'rva', '25846389_mac_patches.tsv')):
        p = l.rstrip('\n').split('\t')
        if p[0] != 'patch': continue
        rva, old, new = int(p[1], 16), bytes.fromhex(p[2]), bytes.fromhex(p[3])
        assert b[base + rva:base + rva + len(old)] == old, hex(rva)
        b[base + rva:base + rva + len(new)] = new; k += 1
    open(GA_HELL + '.tmp', 'wb').write(b)
    subprocess.run(['codesign', '-f', '-s', '-', GA_HELL + '.tmp'], check=True, capture_output=True)
    os.replace(GA_HELL + '.tmp', GA_HELL); log(f'GameAssembly with {k} Hell patches written into the file: {GA_HELL}')
def use_ga(src):
    same = subprocess.run(['cmp', '-s', src, GA]).returncode == 0
    if not same:
        subprocess.run(['cp', '-c', src, GA + '.swap'], check=True); os.replace(GA + '.swap', GA)   # new inode, never written in place

BIE_SCRIPT = os.path.join(REPO, 'app', 'bepinex', 'install_bepinex.sh'); ML_SCRIPT = os.path.join(REPO, 'app', 'melon', 'install_melon.sh')
def install(kind, fresh_tag):
    """a fresh BepInEx / MelonLoader folder (first start) or the last one (later start); install time from the local zip"""
    root = os.path.join(I, f'{kind}_bt'); os.makedirs(root, exist_ok=True)
    d = os.path.join(root, fresh_tag)
    if not os.path.exists(d):
        env = dict(os.environ, **({'DSML_BEPINEX_ZIP': os.environ['BIE_ZIP']} if kind == 'bie' else {'DSML_MELON_ZIP': os.environ['ML_ZIP']}))
        t = time.time(); p = subprocess.run(['/bin/zsh', BIE_SCRIPT if kind == 'bie' else ML_SCRIPT, d], env=env, capture_output=True, text=True)
        log(f'{kind} install {round(time.time() - t, 1)} s: {p.stdout.strip()[-120:]}')
    return d

def case_env(c, run):
    """(dylib to insert first, extra inserts, env, GameAssembly file)"""
    if c in ('a', 'b'): return PROBES['hide'], [], {}, (GA_GATE if c == 'a' else GA_HELL)
    prof = os.path.join(OUT, f'profile_{c}.json')
    on = [] if c in ('c', 'c0') else DEFAULT + (['dsmp-bridge'] if c == 'e' else ['hello-melon'] if c == 'f' else [])
    json.dump({'enabled': on, 'order': on}, open(prof, 'w'))
    root = os.path.join(OUT, 'root'); os.makedirs(os.path.join(root, 'crash'), exist_ok=True); os.makedirs(os.path.join(root, 'loader'), exist_ok=True)
    open(os.path.join(root, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')
    env = {'DSL_MODS_DIR': MODS, 'DSL_PROFILE': prof, 'DSML_ROOT': root, 'DSL_UPDATE': '0', 'DSML_DSLOAD': DSLOAD}
    extra = [PROBES['title']]
    if c == 'c0': env.update({'DSL_FASTBOOT': '0', 'DSL_PREWARM': '0'})
    if c == 'e':
        d = install('bie', f'r{run}')
        extra += [os.path.join(d, 'libdoorstop.dylib')]
        env.update({'DSML_BEPINEX_DIR': d, 'DSML_BEPINEX_SCRIPT': BIE_SCRIPT, 'DOORSTOP_ENABLED': '1', 'DOORSTOP_IGNORE_DISABLED_ENV': '0',
                    'DOORSTOP_TARGET_ASSEMBLY': os.path.join(d, 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'),
                    'DOORSTOP_CLR_RUNTIME_CORECLR_PATH': os.path.join(d, 'dotnet/libcoreclr.dylib'), 'DOORSTOP_CLR_CORLIB_DIR': os.path.join(d, 'dotnet'),
                    'BEPINEX_PLUGIN_DIRS': os.path.join(MODS, 'dsmp-bridge')})
        cfg = os.path.join(d, 'BepInEx', 'config'); os.makedirs(cfg, exist_ok=True)
    if c == 'f':
        d = install('ml', f'r{run}')
        extra += [os.path.join(d, 'MelonLoader.Bootstrap.dylib')]
        env.update({'DSML_MELON_DIR': d, 'DSML_MELON_SCRIPT': ML_SCRIPT, 'MELONLOADER_BASEDIR': d, 'MELONLOADER_MOD_DIRS': os.path.join(MODS, 'hello-melon')})
    return DSLOAD, extra, env, GA_GATE

def seed_stock_data():
    """a stock copy (no loader) keeps its data under its own HOME: the same muted, windowed UserSetting.json as the loader cases"""
    r.prepare_data(X)
    d = os.path.join(I, 'home', 'Library', 'Application Support', 'CanOpener', 'Dungeon Settlers'); os.makedirs(os.path.join(d, 'Saves'), exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        s = os.path.join(I, 'data', f)
        if os.path.exists(s): shutil.copy2(s, os.path.join(d, f))

def clock(line):
    h, m, s = line[:12].split(':'); return int(h) * 3600 + int(m) * 60 + float(s)

def one(c, run, first):
    ins, extra, env, ga = case_env(c, run)        # a later start reuses the folder of this round's first start
    use_ga(ga); seed_stock_data()
    for f in ('player.log', 'dsload.log', 'bootprobe.txt'):
        p = os.path.join(I, f)
        if os.path.exists(p): os.replace(p, os.path.join(OUT, f'prev_{f}'))
    for k in [k for k in os.environ if k.startswith(('DSL_', 'DSML_', 'DOORSTOP_', 'BEPINEX_', 'MELONLOADER_'))]: os.environ.pop(k)
    os.environ['LRIG_DYLIB'] = ins
    if extra: os.environ['LRIG_EXTRA_INSERT'] = ':'.join(extra)
    else: os.environ.pop('LRIG_EXTRA_INSERT', None)
    env['BOOTPROBE_OUT'] = os.path.join(I, 'bootprobe.txt')
    orig = r.ctl_env; r.ctl_env = lambda: {**orig(), **env}          # lrig passes only DSRIG_/DSL_/DSML_ through: give the rest here
    t0 = time.time(); lt0 = time.localtime(t0)
    try: r.start(X, window=True, fps=60)
    finally: r.ctl_env = orig
    pf = os.path.join(I, 'bootprobe.txt'); dl = os.path.join(I, 'dsload.log'); t_title = None; t_done = None; pstart = None
    limit = 900 if first and c in ('e', 'f') else 180
    while time.time() - t0 < limit:
        for l in (open(pf).read().splitlines() if os.path.exists(pf) else []):
            w = l.split()
            if w[1] == 'process-start' and pstart is None: pstart = float(w[0])
            if w[1] == 'state' and w[2] == '1' and w[3] == '1' and t_title is None: t_title = float(w[0])
        if t_title is not None and (c in ('a', 'b') or t_done is not None or time.time() - t_title > 20): break
        if c not in ('a', 'b') and t_done is None and os.path.exists(dl):
            m = [l for l in open(dl, errors='replace').read().splitlines() if 'boot done (title ready' in l]
            if m:
                lt = time.localtime(t0); day0 = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, 0, 0, 0, 0, 0, -1))
                t_done = day0 + clock(m[0])
        if not r.alive(r.read_pid(X)) and time.time() - t0 > 5: log('copy died'); break
        time.sleep(0.05)
    time.sleep(1)
    if c in ('a', 'b'):                                   # stock: no control socket, stop by PID (our copy only: lrig.alive checks it)
        pid = r.read_pid(X)
        if r.alive(pid): os.kill(pid, 9)
    else:
        try: r.stop(X)
        except Exception as e: log('stop:', e)
    for f in ('player.log', 'dsload.log', 'bootprobe.txt'):
        p = os.path.join(I, f)
        if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{c}_{run}{"" if first else "L"}_{f}'))
    z = pstart or t0
    res = {'case': c, 'run': run, 'first': first, 'title_ready_s': t_title and round(t_title - z, 2), 'boot_done_s': t_done and round(t_done - z, 2),
           'spawn_to_process_s': pstart and round(pstart - t0, 2)}
    log(json.dumps(res)); open(os.path.join(OUT, 'results.jsonl'), 'a').write(json.dumps(res) + '\n')
    time.sleep(3)
    return res

pair = None; t_l = time.time()
while not pair and time.time() - t_l < 1800:
    p = subprocess.run([sys.executable, RIG, 'lease', 'modguide', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = p.stdout.strip() if p.returncode == 0 else None
    if not pair: time.sleep(15)
log('lease', pair)
try:
    if not pair: raise SystemExit('no lease')
    for run in range(1, RUNS + 1):                        # round robin: drift of the Mac's load hits every case alike
        for c in CASES:
            if c in ('e', 'f'):
                one(c, run, True); one(c, run, False)
            else:
                one(c, run, True)
finally:
    try: r.stop(X)
    except Exception: pass
    use_ga(GA_GATE)
    if pair: subprocess.run([sys.executable, RIG, 'release', pair, 'modguide'], capture_output=True)
rows = [json.loads(l) for l in open(os.path.join(OUT, 'results.jsonl'))]
for c in CASES:
    for first in ((True, False) if c in ('e', 'f') else (True,)):
        v = [x for x in rows if x['case'] == c and x['first'] == first]
        print(c, 'first' if first else 'later', 'title ready', [x['title_ready_s'] for x in v], 'boot done', [x['boot_done_s'] for x in v])
