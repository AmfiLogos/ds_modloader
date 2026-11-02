#!/usr/bin/env python3
"""Name check (kit 0.1.32, ../NOTES.md "Name check") in the real game, on ONE hidden Mac test copy (default NVN; test
copies only, never the owner's install, saves, prefs or screen).

The real break this catches: a mod that needs a game name this game version lacks starts anyway and breaks in the middle
of a world (it must be OFF at start, named); or the check turns off a mod whose names all exist (real il2cpp lookups).
Mods: names-good and names-gone, both built from test/mods_src/names-probe (GONE=0 / GONE=1). Flow: start, title ready,
stop. FAILS on: names-good not loaded or a wrong lookup, names-gone loaded or no NAME-MISS line, no title.
Usage: names_test.py <out> [--copy NVN] [--dsload PATH] [--parse <dsload.log>]
  --dsload: default this tree's loader/build/dsload.dylib (LOADER_ONLY=1 zsh loader/build.sh). About 2 minutes.
Last line: RESULT names PASS|FAIL <failed checks>"""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
import events_test as E                                 # its helpers only (driver, builds, owner check)

a = sys.argv[1:]
if not a or a[0].startswith('-'): raise SystemExit(__doc__)
OUT = os.path.abspath(a[0]); opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
X = opt('--copy', 'NVN'); I = r.idir(X); E.X = X
LOADER = os.path.dirname(HERE)
DSLOAD = os.path.abspath(os.path.expanduser(opt('--dsload', os.path.join(LOADER, 'build', 'dsload.dylib'))))
PROBE = os.path.join(HERE, 'mods_src', 'names-probe', 'names_probe.c')
log = lambda *x: print(time.strftime('%H:%M:%S'), X, *x, flush=True)


def parse(text):
    L = text.splitlines(); out = []
    chk = lambda n, ok, d='': out.append((n, bool(ok), d))
    g = [l for l in L if 'NAMEPROBE good' in l]
    m = re.search(r'class=1 code=1 method=1 field=(\d+)', g[-1]) if g else None
    chk('names-good: real class, method code, MethodInfo and field found', m and int(m.group(1)) > 0, g[-1].strip() if g else 'no line')
    chk('names-good: loaded, no miss', any('names-good 0.1.0: loaded' in l for l in L) and not any('NAME-MISS names-good' in l for l in L))
    chk('names-gone: the missing class is logged', any('NAME-MISS names-gone Refactor.UI.NoSuchView_NameCheck' in l for l in L))
    chk('names-gone: off at start with the name', any('names-gone: not for this game version: Refactor.UI.NoSuchView_NameCheck missing' in l for l in L)
        and not any('names-gone 0.1.0: loaded' in l for l in L))
    return out


def run(rdir):
    if E.owner_playing(): raise SystemExit('the owner\'s game runs: not starting a test copy')
    if not os.path.exists(os.path.join(I, r.EXE_REL)): raise SystemExit(f'no copy {X}: run loader/test/make_instance.sh {X}')
    if r.alive(r.read_pid(X)): raise SystemExit(f'{X} already runs')
    E.wait_load(40.0)
    os.makedirs(rdir, exist_ok=True)
    drv = E.build(E.DRV_SRC, os.path.join(OUT, 'scan_drv.dylib'), ['clang', '-arch', 'arm64', '-O2', '-fexceptions', '-dynamiclib', '-framework', 'AppKit', '-lobjc'])
    fake = E.build(E.FAKE_SRC, os.path.join(OUT, 'fake_steam_api.dylib'), ['clang', '-O0', '-dynamiclib'])
    mods = os.path.join(OUT, 'mods')
    for mid, gone in (('names-good', 0), ('names-gone', 1)):
        pm = os.path.join(mods, mid); os.makedirs(pm, exist_ok=True)
        subprocess.run(['nice', '-n', '19', 'clang', '-arch', 'arm64', '-O2', '-Wall', f'-DGONE={gone}', '-I' + os.path.join(LOADER, 'sdk'),
                        '-dynamiclib', '-o', os.path.join(pm, mid + '.dylib'), PROBE], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pm, mid + '.dylib')], capture_output=True)
        json.dump({'id': mid, 'name': mid + ' (test only)', 'version': '0.1.0', 'kind': 'code', 'api': 3, 'scope': 'client',
                   'game_builds': ['*'], 'platforms': {'mac-arm64': {'entry': mid + '.dylib'}}}, open(os.path.join(pm, 'mod.json'), 'w'), indent=1)
    pd = os.path.join(rdir, 'drv'); os.makedirs(pd, exist_ok=True); D = E.Drv(pd)
    home_data = os.path.join(I, 'home', 'Library', 'Application Support', 'CanOpener', 'Dungeon Settlers')
    r.prepare_data(X)
    os.makedirs(home_data, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        s = os.path.join(I, 'data', f)
        if os.path.exists(s): shutil.copy2(s, os.path.join(home_data, f))
    root = os.path.join(rdir, 'root'); os.makedirs(os.path.join(root, 'crash'), exist_ok=True); os.makedirs(os.path.join(root, 'loader'), exist_ok=True)
    open(os.path.join(root, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')
    prof = os.path.join(rdir, 'profile.json'); json.dump({'enabled': ['names-good', 'names-gone'], 'order': ['names-good', 'names-gone']}, open(prof, 'w'))
    cloud = os.path.join(rdir, 'fake_cloud'); os.makedirs(cloud, exist_ok=True)
    env = {'SCAN_DRV_DIR': pd, 'DSL_MODS_DIR': mods, 'DSL_PROFILE': prof, 'DSML_ROOT': root, 'DSL_UPDATE': '0',
           'DSML_DSLOAD': DSLOAD, 'DSML_DATA': home_data, 'DSL_CLOUD_API': fake, 'FAKE_CLOUD_DIR': cloud, 'DSRIG_LIVE': '1'}
    for k in [k for k in os.environ if k.startswith(('DSL_', 'DSML_', 'DSRIG_', 'FAKE_', 'LRIG_'))]: os.environ.pop(k)
    os.environ['LRIG_DYLIB'] = DSLOAD; os.environ['LRIG_EXTRA_INSERT'] = drv
    orig = r.ctl_env
    r.ctl_env = lambda: {k: v for k, v in {**orig(), **env}.items() if k != 'DSRIG_CTL'}
    for f in ('state.txt', 'cmd.out', 'cmd.txt', 'cmd.taken'):
        q = os.path.join(pd, f)
        if os.path.exists(q): os.replace(q, q + '.prev')
    title = False
    try:
        r.start(X, fps=60)
    finally:
        r.ctl_env = orig
    try:
        D.wait(lambda s: s.get('scene') == '1' and s.get('boot') == '1' and s.get('busy') == '0', 'title ready', 400)
        title = True; log('title ready'); time.sleep(2)
    finally:
        r.stop(X)
        for f in ('player.log', 'dsload.log', 'guard.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(rdir, f))
        log('owner prefs', 'unchanged' if r.check_prefs() else 'CHANGED')
    return title


def report(text, title):
    res = parse(text)
    if title is not None: res.append(('the game reached the title', title, ''))
    for n, ok, d in res: print(f'  {"ok  " if ok else "FAIL"} {n}' + (f': {d}' if d else ''))
    return [n for n, ok, _ in res if not ok]


def main():
    os.makedirs(OUT, exist_ok=True)
    if '--parse' in a:
        f = report(open(opt('--parse'), errors='replace').read(), None)
        print(f'RESULT names {"PASS" if not f else "FAIL"} {len(f)}'); sys.exit(1 if f else 0)
    rig = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
    lp = subprocess.run([sys.executable, rig, 'lease', 'nvnames', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = lp.stdout.strip()
    if lp.returncode or not pair: raise SystemExit('no lease: ' + (lp.stderr.strip() or 'no free pair'))
    log('lease', pair); fails = []
    try:
        title = run(os.path.join(OUT, 'run'))
        p = os.path.join(OUT, 'run', 'dsload.log')
        fails = report(open(p, errors='replace').read() if os.path.exists(p) else '', title)
    finally:
        subprocess.run([sys.executable, rig, 'release', pair, 'nvnames'], capture_output=True)
    json.dump({'ok': not fails, 'failed': fails}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    print(f'RESULT names {"PASS" if not fails else "FAIL"} {len(fails)}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
