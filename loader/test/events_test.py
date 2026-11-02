#!/usr/bin/env python3
"""Loader events (SDK API 7, src/dsl_events.c) on ONE hidden Mac test copy (default NVE; test copies only, never the
owner's install, saves, prefs or screen; one copy at a time).

The real break this catches: a mod that listens (dsl_on_event) misses a scene change, a world load, a game window
shown / hidden or a language change, or gets it from a scene search; a game update moves a call the view hooks rely on.
The loader logs "EVENT-RULE missed ..." in a test copy when a view hook that is on did not report a window (its records
check did). Mods: only the test module ev-probe (test/mods_src/ev-probe: logs one EVPROBE line per event).

Two runs (each a fresh start of the copy): hooks (the normal start) and records (DSL_EV_NOHOOK=1: no view hook, the
loader's records check sends the view events; the fallback when a game update refuses a hook site).
Flow per run (loader/test/scanrate/scan_drv.c drives the copy by file commands, it hooks nothing): title; ev-probe
switches the game's language static to German and back (hooks run only); Load list open + closed; the wiki's public base
save loaded under a fresh world id; the Esc menu (SystemMenu popup) open + closed.
Each run FAILS on: a missing EVPROBE line (scene 1 and 2, world loaded with the save's guid, view created / shown /
hidden of TitleMenuView, SaveLoadView, SystemMenuView, shown of TooltipUI and QuickSlotUI, title and world frames,
language 3 then back), any EVENT-RULE line (hooks run), any SCAN-RULE line, a probe that is not subscribed.

Usage: events_test.py <out> [--copy NVE] [--dsload PATH] [--only hooks|records] [--parse <dsload.log> hooks|records]
  --dsload: default this tree's loader/build/dsload.dylib (zsh loader/build.sh, LOADER_ONLY=1). About 5 minutes.
Last line: RESULT events PASS|FAIL <failed checks>"""
import json, os, re, shutil, subprocess, sys, time, uuid
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r

a = sys.argv[1:] if __name__ == '__main__' else ['-']           # imported (win/events_vm.py): parse() only
if __name__ == '__main__' and (not a or a[0].startswith('-')): raise SystemExit(__doc__)
OUT = os.path.abspath(a[0])
opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
X = opt('--copy', 'NVE'); I = r.idir(X)
LOADER = os.path.dirname(HERE)
DSLOAD = os.path.abspath(os.path.expanduser(opt('--dsload', os.path.join(LOADER, 'build', 'dsload.dylib'))))
BASE = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public (the wiki's base save)
FAKE_SRC = os.path.join(HERE, 'cloud', 'fake_steam_api.c')
DRV_SRC = os.path.join(HERE, 'scanrate', 'scan_drv.c')
PROBE_SRC = os.path.join(HERE, 'mods_src', 'ev-probe')
log = lambda *x: print(time.strftime('%H:%M:%S'), X, *x, flush=True)


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
        subprocess.run(['nice', '-n', '19'] + cmd + ['-o', out, src], check=True)
    return out


class Drv:
    def __init__(self, d): self.d = d
    def state(self):
        s = {}
        try:
            for l in open(os.path.join(self.d, 'state.txt')):
                k, _, v = l.rstrip('\n').partition(' '); s[k] = v
        except OSError:
            pass
        return s
    def wait(self, pred, what, timeout):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if not r.alive(r.read_pid(X)): raise RuntimeError(f'copy died waiting for {what}')
            s = self.state()
            if s and pred(s): return s
            time.sleep(0.5)
        raise RuntimeError(f'timeout waiting for {what}: {self.state()}')
    def cmd(self, line, timeout=10):
        out = os.path.join(self.d, 'cmd.out'); n0 = len(open(out).readlines()) if os.path.exists(out) else 0
        p = os.path.join(self.d, 'cmd.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
        t0 = time.time()
        while time.time() - t0 < timeout:
            if os.path.exists(out):
                ls = open(out).readlines()
                if len(ls) > n0: log('cmd', line, '->', ls[-1].strip()); return ls[-1].split()[1] == 'ok'
            time.sleep(0.2)
        log('cmd', line, '-> no answer'); return False


def world_save(path, guid, stem, clan):
    w = json.load(open(BASE)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


# ------------------------------------------------------------------ the checks (also --parse)
EV = re.compile(r'EVPROBE (view-created|view-shown|view-hidden) (\S+) kind=(\S+) id=(-?\d+) scene=(-?\d+) obj=(\S+) shown_now=(\d)')
def parse(text, mode, guid=None):
    """dsload.log -> list of (name, ok, detail)"""
    L = text.splitlines(); ev = [m.groups() for l in L for m in [EV.search(l)] if m]
    seen = lambda kind, cls: [e for e in ev if e[0] == kind and e[1] == cls]
    out = []
    def chk(name, ok, detail=''): out.append((name, bool(ok), detail))
    sub = [l for l in L if 'EVPROBE subscribed' in l]
    chk('probe subscribed, duplicate refused', sub and 'subscribed ok; duplicate refused' in sub[-1], sub[-1].strip() if sub else 'no line')
    hk = [l for l in L if 'events: view hooks:' in l or 'DSL_EV_NOHOOK=1' in l]
    if mode == 'hooks': chk('view hooks all on', hk and 'created on, shown on, hidden on, scene clear on / on' in hk[-1], hk[-1].strip() if hk else 'no line')
    else: chk('no view hooks (records mode)', hk and 'DSL_EV_NOHOOK=1' in hk[-1], hk[-1].strip() if hk else 'no line')
    sc = [(int(m.group(1)), int(m.group(2))) for l in L for m in [re.search(r'EVPROBE scene (-?\d+) -> (-?\d+)', l)] if m]
    chk('scene events: the title, then a world', any(s == 1 for _, s in sc) and any(s == 2 for _, s in sc), str(sc))
    wl = [l.split('EVPROBE world-loaded ')[1].split()[0] for l in L if 'EVPROBE world-loaded ' in l]
    chk('world loaded with the save\'s guid', wl and (guid is None or guid in wl), str(wl))
    fr = [tuple(int(v) for v in m.groups()) for l in L for m in [re.search(r'frames title=(\d+) world=(\d+) any=(\d+)', l)] if m]
    ent = [tuple(int(v) for v in m.groups()) for l in L for m in [re.search(r'EVPROBE scene -?\d+ -> 2 frames title=(\d+) world=(\d+) any=(\d+)', l)] if m]
    chk('frame filters: title frames before the world, no world frame before it', ent and ent[0][0] > 0 and ent[0][1] == 0 and ent[0][2] > ent[0][0], str(ent[:1]))
    chk('world frames counted in the world', any(w > 0 for _, w, _ in fr), str(fr[-1:]))
    ld = [tuple(int(v) for v in m.groups()) for l in L for m in [re.search(r'loading=(\d+) bad=(\d+)', l)] if m]
    chk('no title / world frame while a scene or map loads (loading frames seen)', ld and ld[-1][0] > 0 and ld[-1][1] == 0, str(ld[-1:]))
    for cls, kind in (('TitleMenuView', 'static'), ('SaveLoadView', 'popup'), ('SystemMenuView', 'popup')):
        c, s, h = seen('view-created', cls), seen('view-shown', cls), seen('view-hidden', cls)
        chk(f'{cls}: created, shown, hidden ({kind})', c and s and h and all(e[2] == kind for e in c + s + h) and all(e[6] == '1' for e in s),
            f'created {len(c)} shown {len(s)} hidden {len(h)}')
    for cls in ('TooltipUI', 'QuickSlotUI'):
        s = seen('view-shown', cls); chk(f'{cls}: shown (static)', s and all(e[2] == 'static' for e in s), f'shown {len(s)}')
    ids = {e[1]: e[3] for e in ev if e[2] == 'popup'}
    chk('popup ids: SaveLoadView 5, SystemMenuView 1', ids.get('SaveLoadView') == '5' and ids.get('SystemMenuView') == '1', str({k: ids.get(k) for k in ('SaveLoadView', 'SystemMenuView')}))
    if mode == 'hooks':
        lg = [l.split('EVPROBE lang ')[1].strip() for l in L if 'EVPROBE lang ' in l]
        chk('language events: German, then back', len(lg) >= 2 and lg[0].startswith('3 de') and not lg[-1].startswith('3 '), str(lg))
        rules = [l.strip() for l in L if 'EVENT-RULE' in l]
        chk('no EVENT-RULE line (every window came from its hook)', not rules, '; '.join(rules[:3]))
    scan = [l.strip() for l in L if 'SCAN-RULE' in l]
    chk('no SCAN-RULE line', not scan, '; '.join(scan[:3]))
    others = sorted({(e[1], e[2]) for e in ev} - {('TitleMenuView', 'static'), ('SaveLoadView', 'popup'), ('SystemMenuView', 'popup'), ('TooltipUI', 'static'), ('QuickSlotUI', 'static')})
    chk('(info) other windows reported', True, ', '.join(f'{c} {k}' for c, k in others))
    return out


# ------------------------------------------------------------------ one run
def run(mode, rdir):
    if owner_playing(): raise SystemExit('the owner\'s game runs: not starting a test copy')
    if not os.path.exists(os.path.join(I, r.EXE_REL)): raise SystemExit(f'no copy {X}: run loader/test/make_instance.sh {X}')
    if r.alive(r.read_pid(X)): raise SystemExit(f'{X} already runs')
    if not os.path.exists(DSLOAD): raise SystemExit(f'no loader at {DSLOAD} (LOADER_ONLY=1 zsh loader/build.sh, or --dsload)')
    wait_load()
    os.makedirs(rdir, exist_ok=True)
    drv = build(DRV_SRC, os.path.join(OUT, 'scan_drv.dylib'), ['clang', '-arch', 'arm64', '-O2', '-fexceptions', '-dynamiclib', '-framework', 'AppKit', '-lobjc'])
    fake = build(FAKE_SRC, os.path.join(OUT, 'fake_steam_api.dylib'), ['clang', '-O0', '-dynamiclib'])
    mods = os.path.join(OUT, 'mods'); pm = os.path.join(mods, 'ev-probe'); os.makedirs(pm, exist_ok=True)
    build(os.path.join(PROBE_SRC, 'ev_probe.c'), os.path.join(pm, 'ev-probe.dylib'), ['clang', '-arch', 'arm64', '-O2', '-Wall', '-I' + os.path.join(LOADER, 'sdk'), '-dynamiclib'])
    shutil.copy2(os.path.join(PROBE_SRC, 'mod.json'), pm)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pm, 'ev-probe.dylib')], capture_output=True)
    pd = os.path.join(rdir, 'drv'); os.makedirs(pd, exist_ok=True); D = Drv(pd)
    home_data = os.path.join(I, 'home', 'Library', 'Application Support', 'CanOpener', 'Dungeon Settlers')
    aside = os.path.join(OUT, 'old_data'); os.makedirs(aside, exist_ok=True)
    for d in (os.path.join(I, 'data'), home_data):          # every run starts with no saves but its own world (moved, never deleted)
        for s in ('Saves', 'Mods', 'SaveHeaderCache.json', 'fe', 'mp_owners'):
            q = os.path.join(d, s)
            if os.path.exists(q): shutil.move(q, os.path.join(aside, (os.path.basename(d) + '_' + s).replace(' ', '_') + time.strftime('_%H%M%S')))
    r.prepare_data(X)
    os.makedirs(os.path.join(home_data, 'Saves'), exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        s = os.path.join(I, 'data', f)
        if os.path.exists(s): shutil.copy2(s, os.path.join(home_data, f))
    guid = str(uuid.uuid4()); stem = 'EvTest__AutoSave_' + guid.replace('-', '') + '_0'
    world_save(os.path.join(home_data, 'Saves', stem + '.json'), guid, stem, 'Evtest')
    root = os.path.join(rdir, 'root'); os.makedirs(os.path.join(root, 'crash'), exist_ok=True); os.makedirs(os.path.join(root, 'loader'), exist_ok=True)
    open(os.path.join(root, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')
    prof = os.path.join(rdir, 'profile.json'); json.dump({'enabled': ['ev-probe'], 'order': ['ev-probe']}, open(prof, 'w'))
    cloud = os.path.join(rdir, 'fake_cloud'); os.makedirs(cloud, exist_ok=True)
    env = {'SCAN_DRV_DIR': pd, 'DSL_MODS_DIR': mods, 'DSL_PROFILE': prof, 'DSML_ROOT': root, 'DSL_UPDATE': '0',
           'DSML_DSLOAD': DSLOAD, 'DSML_DATA': home_data, 'DSL_CLOUD_API': fake, 'FAKE_CLOUD_DIR': cloud, 'DSRIG_LIVE': '1'}
    if mode == 'hooks': env['EVPROBE_LANG'] = '1'
    else: env['DSL_EV_NOHOOK'] = '1'
    for k in [k for k in os.environ if k.startswith(('DSL_', 'DSML_', 'DSRIG_', 'FAKE_', 'LRIG_', 'EVPROBE_'))]: os.environ.pop(k)
    os.environ['LRIG_DYLIB'] = DSLOAD; os.environ['LRIG_EXTRA_INSERT'] = drv
    orig = r.ctl_env
    r.ctl_env = lambda: {k: v for k, v in {**orig(), **env}.items() if k != 'DSRIG_CTL'}   # no control socket (a player's game)
    home = os.path.expanduser('~')
    json.dump({'copy': X, 'mode': mode, 'dsload': DSLOAD.replace(home, '~'), 'env': {k: v.replace(home, '~') for k, v in env.items()},
               'save': stem, 'load1': os.getloadavg()[0]}, open(os.path.join(rdir, 'run.json'), 'w'), indent=1)
    for f in ('state.txt', 'cmd.out', 'cmd.txt', 'cmd.taken'):
        q = os.path.join(pd, f)
        if os.path.exists(q): os.replace(q, q + '.prev')
    steps = {}
    try:
        r.start(X, fps=60)
    finally:
        r.ctl_env = orig
    try:
        D.wait(lambda s: s.get('swapped') not in (None, '0'), 'pointer swap', 120)
        D.wait(lambda s: s.get('scene') == '1' and s.get('boot') == '1' and s.get('busy') == '0', 'title ready', 400)
        log('title ready'); time.sleep(8 if mode == 'hooks' else 3)
        steps['openload'] = D.cmd('openload'); time.sleep(3)
        steps['close_list'] = D.cmd('unpopup 5'); time.sleep(2)
        steps['load'] = D.cmd(f'load {stem}')
        D.wait(lambda s: s.get('scene') == '2' and s.get('busy') == '0' and int(s.get('tick', -1)) > 0, 'world loaded', 400)
        log('world loaded'); time.sleep(3)
        steps['esc_open'] = D.cmd('popup 1'); time.sleep(2)
        steps['esc_close'] = D.cmd('unpopup 1'); time.sleep(2)
    finally:
        r.stop(X)
        for f in ('player.log', 'dsload.log', 'guard.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(rdir, f))
        log('owner prefs', 'unchanged' if r.check_prefs() else 'CHANGED')
    return steps, guid


def report(mode, text, guid, steps):
    res = parse(text, mode, guid)
    flow = all(steps.get(k) for k in ('openload', 'close_list', 'load', 'esc_open', 'esc_close')) if steps is not None else True
    if steps is not None: res.append(('flow (driver commands answered ok)', flow, json.dumps(steps)))
    for n, ok, d in res: print(f'  {"ok  " if ok else "FAIL"} [{mode}] {n}' + (f': {d}' if d else ''))
    return [n for n, ok, _ in res if not ok]


def main():
    os.makedirs(OUT, exist_ok=True)
    if '--parse' in a:
        i = a.index('--parse'); f = report(a[i + 2] if len(a) > i + 2 else 'hooks', open(a[i + 1], errors='replace').read(), None, None)
        print(f'RESULT events {"PASS" if not f else "FAIL"} {len(f)}'); sys.exit(1 if f else 0)
    rig = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
    lp = subprocess.run([sys.executable, rig, 'lease', 'nvevents', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = lp.stdout.strip()
    if lp.returncode or not pair: raise SystemExit('no lease: ' + (lp.stderr.strip() or 'no free pair'))
    log('lease', pair)
    fails = []
    try:
        for mode in ([opt('--only')] if opt('--only') else ['hooks', 'records']):
            rdir = os.path.join(OUT, mode)
            steps, guid = run(mode, rdir)
            p = os.path.join(rdir, 'dsload.log')
            fails += report(mode, open(p, errors='replace').read() if os.path.exists(p) else '', guid, steps)
    finally:
        subprocess.run([sys.executable, rig, 'release', pair, 'nvevents'], capture_output=True)
    json.dump({'ok': not fails, 'failed': fails}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    print(f'RESULT events {"PASS" if not fails else "FAIL"} {len(fails)}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
