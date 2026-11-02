#!/usr/bin/env python3
"""Mod safety at start (tModLoader study ideas 3 + 4, src/dsl_main.c) on ONE hidden Mac test copy (default NSF; hidden
window, muted; test copies only, never the owner's install, saves, prefs or screen).

The real breaks this catches:
  boot:  two starts in a row died after the modules started and before the title; the next start must run with mods
         off, say why in a notice at the title and reset the count (else the player is stuck in a crash loop).
  crash: the crash guard turned a mod off; a notice at the title must name it, and a mod that REQUIRES it must not run
         (else it runs half-broken).
Mods: the test module ev-probe (test/mods_src/ev-probe) and ev-dep (the same code, requires ev-probe).
Runs (a fresh start each): boot (dsload_state/_boot.pending + _boot.count=1 seeded; DSL_BOOTGUARD=1, test copies have
it off), crash (dsload_state/ev-probe.pending seeded: as if the game died in ev-probe's init last time). Each run takes
one game frame of the notice into <out>/<run>/notice_<run>.png (for the lead; not a check).

Usage: safety_test.py <out> [--copy NSF] [--dsload PATH] [--only boot|crash]
  --dsload: default this tree's loader/build/dsload.dylib (LOADER_ONLY=1 zsh loader/build.sh). About 3 minutes.
Last line: RESULT safety PASS|FAIL <failed checks>"""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r

a = sys.argv[1:]
if not a or a[0].startswith('-'): raise SystemExit(__doc__)
OUT = os.path.abspath(a[0])
opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
X = opt('--copy', 'NSF'); I = r.idir(X); SD = os.path.join(I, 'dsload_state')
LOADER = os.path.dirname(HERE)
DSLOAD = os.path.abspath(os.path.expanduser(opt('--dsload', os.path.join(LOADER, 'build', 'dsload.dylib'))))
FAKE_SRC = os.path.join(HERE, 'cloud', 'fake_steam_api.c')
DRV_SRC = os.path.join(HERE, 'scanrate', 'scan_drv.c')
PROBE_SRC = os.path.join(HERE, 'mods_src', 'ev-probe')
log = lambda *x: print(time.strftime('%H:%M:%S'), X, *x, flush=True)


def owner_playing():
    p = subprocess.run(['pgrep', '-f', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/'],
                       capture_output=True, text=True).stdout.split()
    return [x for x in p if x]


def build(src, out, cmd):
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        subprocess.run(['nice', '-n', '19'] + cmd + ['-o', out, src], check=True)
    return out


def state(d):
    s = {}
    try:
        for l in open(os.path.join(d, 'state.txt')):
            k, _, v = l.rstrip('\n').partition(' '); s[k] = v
    except OSError:
        pass
    return s


def wait(pred, what, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if not r.alive(r.read_pid(X)): raise RuntimeError(f'copy died waiting for {what}')
        v = pred()
        if v: return v
        time.sleep(0.5)
    raise RuntimeError(f'timeout waiting for {what}')


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def set_aside(names):
    old = os.path.join(OUT, 'old_state'); os.makedirs(old, exist_ok=True)
    for n in names:
        q = os.path.join(SD, n)
        if os.path.exists(q): shutil.move(q, os.path.join(old, n + time.strftime('_%H%M%S')))


def run(mode, rdir):
    if r.alive(r.read_pid(X)): raise SystemExit(f'{X} already runs')
    if not os.path.exists(DSLOAD): raise SystemExit(f'no loader at {DSLOAD}')
    os.makedirs(rdir, exist_ok=True)
    drv = build(DRV_SRC, os.path.join(OUT, 'scan_drv.dylib'), ['clang', '-arch', 'arm64', '-O2', '-fexceptions', '-dynamiclib', '-framework', 'AppKit', '-lobjc'])
    fake = build(FAKE_SRC, os.path.join(OUT, 'fake_steam_api.dylib'), ['clang', '-O0', '-dynamiclib'])
    mods = os.path.join(OUT, 'mods')
    for mid in ('ev-probe', 'ev-dep'):
        pm = os.path.join(mods, mid); os.makedirs(pm, exist_ok=True)
        build(os.path.join(PROBE_SRC, 'ev_probe.c'), os.path.join(pm, mid + '.dylib'), ['clang', '-arch', 'arm64', '-O2', '-Wall', '-I' + os.path.join(LOADER, 'sdk'), '-dynamiclib'])
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pm, mid + '.dylib')], capture_output=True)
        mf = json.load(open(os.path.join(PROBE_SRC, 'mod.json')))
        if mid == 'ev-dep':
            mf.update({'id': 'ev-dep', 'name': 'Needs the events probe (test only)', 'requires': ['ev-probe']})
        mf['platforms'] = {'mac-arm64': {'entry': mid + '.dylib'}}
        json.dump(mf, open(os.path.join(pm, 'mod.json'), 'w'), indent=1)
    os.makedirs(SD, exist_ok=True)
    set_aside([n for n in os.listdir(SD) if n.startswith('_boot.') or n.startswith('ev-')])
    if mode == 'boot':
        open(os.path.join(SD, '_boot.pending'), 'w').write('modules started, title not reached\n')
        open(os.path.join(SD, '_boot.count'), 'w').write('1\n')
    else:
        open(os.path.join(SD, 'ev-probe.pending'), 'w').write('dsl_plugin_init')
    pd = os.path.join(rdir, 'drv'); os.makedirs(pd, exist_ok=True)
    for f in ('state.txt', 'cmd.out', 'cmd.txt', 'cmd.taken'):
        q = os.path.join(pd, f)
        if os.path.exists(q): os.replace(q, q + '.prev')
    crt = os.path.join(rdir, 'crt'); os.makedirs(crt, exist_ok=True)
    root = os.path.join(rdir, 'root'); os.makedirs(os.path.join(root, 'crash'), exist_ok=True); os.makedirs(os.path.join(root, 'loader'), exist_ok=True)
    open(os.path.join(root, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')
    prof = os.path.join(rdir, 'profile.json'); json.dump({'enabled': ['ev-probe', 'ev-dep'], 'order': ['ev-probe', 'ev-dep']}, open(prof, 'w'))
    cloud = os.path.join(rdir, 'fake_cloud'); os.makedirs(cloud, exist_ok=True)
    home_data = os.path.join(I, 'home', 'Library', 'Application Support', 'CanOpener', 'Dungeon Settlers')
    r.prepare_data(X)
    os.makedirs(os.path.join(home_data, 'Saves'), exist_ok=True)      # the game's own data folder in the copy: windowed
    for f in ('UserSetting.json', 'OutGame.json'):                     # settings + language picked (else the first-start
        q = os.path.join(I, 'data', f)                                 # language box covers the title: no idle title)
        if os.path.exists(q): shutil.copy2(q, os.path.join(home_data, f))
    # never DSRIG_LIVE here: it turns the hidden-window rule (dsl_quiet_mac.c) off; a window run showed on screen (2026-11-02)
    env = {'SCAN_DRV_DIR': pd, 'DSL_MODS_DIR': mods, 'DSL_PROFILE': prof, 'DSML_ROOT': root, 'DSL_UPDATE': '0',
           'DSML_DSLOAD': DSLOAD, 'DSML_DATA': home_data, 'DSL_CLOUD_API': fake, 'FAKE_CLOUD_DIR': cloud,
           'DSL_CRASHREP_TEST': crt, 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NOTCH': '0',
           'DSL_PREWARM': '0', **r.soft_test_env()}
    if mode == 'boot': env['DSL_BOOTGUARD'] = '1'
    for k in [k for k in os.environ if k.startswith(('DSL_', 'DSML_', 'DSRIG_', 'FAKE_', 'LRIG_', 'EVPROBE_'))]: os.environ.pop(k)
    os.environ['LRIG_DYLIB'] = DSLOAD; os.environ['LRIG_EXTRA_INSERT'] = drv
    orig = r.ctl_env
    r.ctl_env = lambda: {**orig(), **env}       # keep DSRIG_CTL: the test-copy rules (hidden, staged window) need it
    home = os.path.expanduser('~')
    json.dump({'copy': X, 'mode': mode, 'dsload': DSLOAD.replace(home, '~'), 'env': {k: v.replace(home, '~') for k, v in env.items()}},
              open(os.path.join(rdir, 'run.json'), 'w'), indent=1)
    after = {}
    try:
        r.start(X, window=True, fps=30)
    finally:
        r.ctl_env = orig
    try:
        wait(lambda: (lambda s: s.get('scene') == '1' and s.get('boot') == '1' and s.get('busy') == '0')(state(pd)), 'title ready', 400)
        log('title ready')
        wait(lambda: 'SAFETY-NOTICE' in dlog(), 'SAFETY-NOTICE', 40)
        time.sleep(2)
        open(os.path.join(crt, 'shot_notice_' + mode), 'w').close()
        try: wait(lambda: os.path.exists(os.path.join(crt, f'notice_{mode}.png')), 'notice shot', 20)
        except RuntimeError as e: log(str(e))
        after = {n: os.path.exists(os.path.join(SD, n)) for n in ('_boot.pending', '_boot.count')}
    except RuntimeError as e:
        log('FLOW:', e)
    finally:
        r.stop(X)
        for f in ('player.log', 'dsload.log', 'guard.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(rdir, f))
        q = os.path.join(crt, f'notice_{mode}.png')
        if os.path.exists(q): shutil.copy2(q, os.path.join(rdir, f'notice_{mode}.png'))
        set_aside([n for n in os.listdir(SD) if n.startswith('_boot.') or n.startswith('ev-')])   # the copy starts clean next time
        log('owner prefs', 'unchanged' if r.check_prefs() else 'CHANGED')
    return after


def parse(text, mode, after):
    out = []
    def chk(name, ok): out.append((name, bool(ok)))
    if mode == 'boot':
        chk('boot guard: 2 dead starts in a row -> this start runs with mods off', 'BOOT GUARD: 2 starts in a row ended before the title: this start runs with mods off' in text)
        chk('no mod runs in the mods-off start', not re.search(r'ev-(probe|dep) [0-9.]+: loaded', text))
        chk('the notice says why (one button, opens the Mods window)', 'SAFETY-NOTICE boot-guard' in text and re.search(r'ui: notice \d+ shown \(one button\): The game closed twice in a row', text))
        chk('the title reset the boot guard (no marker, no count)', after and not after.get('_boot.pending') and not after.get('_boot.count'))
    else:
        chk('crash guard turned ev-probe off', 'CRASH GUARD: the game stopped last time while module ev-probe ran' in text)
        chk('cascade: ev-dep (requires ev-probe) does not run', 'ev-dep: OFF this session: it requires ev-probe, which did not start' in text and not re.search(r'ev-dep [0-9.]+: loaded', text))
        chk('the notice names the mod (Mods / OK)', re.search(r'SAFETY-NOTICE crash-guard: (Loader events probe \(test only\)|ev-probe)', text) and re.search(r'ui: notice \d+ shown \(two buttons\): (Loader events probe \(test only\)|ev-probe) stopped the game', text))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    if not os.path.exists(os.path.join(I, r.EXE_REL)): raise SystemExit(f'no copy {X}: run loader/test/make_instance.sh {X}')
    if owner_playing(): log('the owner\'s game runs: one hidden test copy only (lane rule)')
    rig = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
    lp = subprocess.run([sys.executable, rig, 'lease', 'nvsafety', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = lp.stdout.strip()
    if lp.returncode or not pair: raise SystemExit('no lease: ' + (lp.stderr.strip() or 'no free pair'))
    log('lease', pair)
    fails = []
    try:
        for mode in ([opt('--only')] if opt('--only') else ['boot', 'crash']):
            rdir = os.path.join(OUT, mode)
            after = run(mode, rdir)
            p = os.path.join(rdir, 'dsload.log')
            for n, ok in parse(open(p, errors='replace').read() if os.path.exists(p) else '', mode, after):
                print(f'  {"ok  " if ok else "FAIL"} [{mode}] {n}')
                if not ok: fails.append(f'{mode}: {n}')
    finally:
        subprocess.run([sys.executable, rig, 'release', pair, 'nvsafety'], capture_output=True)
    json.dump({'ok': not fails, 'failed': fails}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    print(f'RESULT safety {"PASS" if not fails else "FAIL"} {len(fails)}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
