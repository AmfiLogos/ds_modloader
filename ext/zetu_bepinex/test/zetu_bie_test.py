#!/usr/bin/env python3
"""ZET-U Mod Suite as a BepInEx plugin, on a hidden Mac rig copy with our BepInEx arm64 build (no mod loader).
One rig lease (~/ds_multiplayer/rig), the test copy cap first. Never the owner's game, saves or prefs: the seed save
(2026-10-11 backup, 7 units) is loaded from copies in <out>.

  zetu_bie_test.py run [out]      max_units 8: the whole proof below; exit 1 on a failed check
  zetu_bie_test.py fresh [out]    no config file: BepInEx writes it with the defaults, the plugin changes no party size
  zetu_bie_test.py disable [out]  the mod loader with its own zetu-suite module in the same game: this plugin stays off
  zetu_bie_test.py cmd '<json>' | probe '<line>'   to the running copy

Env: ZETU_RIG_DYLIB = the rig control dylib (default ~/ds_zetubie/rigbuild/dsrig.dylib), ZETU_BIE = BepInEx install (default ~/ds_zetubie/bie, unzipped bepinex-mac-arm64 0.1.3 = the launcher's), ZETU_BUILD =
the plugin build folder (default ../build). The plugin and the test probe are copied to BepInEx/plugins/<name>/ (the
release layout); the config is written before the start.

Real breaks the run catches:
 W  the save warning: a save loads only after Load; Cancel loads nothing and copies nothing; Load first copies the
    world's save files (same bytes) to BepInEx/ZetuSuite/save_backups/<date>_<clan>/; asked once per world
 1  the game's party cap follows the setting (8 units selected in the expedition window, not 4)
 2  the supplies step has a panel per selected unit (8), in one row, none overlapping
 3  the region step: 8 portraits in one row, none overlapping; the count text says "/8"
 4  the 8 units arrive in the dungeon
 5  Inventory / Status / Skill windows with all 8 selected: 8 panels each, inside the screen, no exception
 S  skills without their weapon: weapons taken off in the dungeon (the game's unequip), then the player's own skill
    order for Greatsword / FireStaff skills: the game asks the bypass and gets yes, the cast runs with an empty hand
    (its durability cost skipped), no exception
 7  the risk "Insufficient units selected" uses the setting: shown for 4 of 8, not for 8
 6  the result screen lists the 8 units"""
import hashlib, json, os, re, shutil, subprocess, sys, time
H = os.path.expanduser('~')
sys.path.insert(0, f'{H}/ds_multiplayer/rig')
import rig as r
HERE = os.path.dirname(os.path.abspath(__file__))
BIE = os.environ.get('ZETU_BIE') or f'{H}/ds_zetubie/bie'
BUILD = os.environ.get('ZETU_BUILD') or os.path.join(os.path.dirname(HERE), 'build')
SEED = os.path.join(r.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
SEED_GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
ENVRESTORE = f'{H}/ds_multiplayer/research/loadertest.noindex/envrestore.dylib'
RIG_DYLIB = os.environ.get('ZETU_RIG_DYLIB') or f'{H}/ds_zetubie/rigbuild/dsrig.dylib'   # control socket; built from ds_multiplayer main (token channel)
X = None; I = None
RESTORE = []                                           # (kept, original) moved back after the run
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def blog():
    try: return open(os.path.join(BIE, 'BepInEx', 'LogOutput.log'), errors='replace').read()
    except OSError: return ''
def plog():
    try: return open(os.path.join(I, 'player.log'), errors='replace').read()
    except OSError: return ''
def zl(L): return [l for l in L.splitlines() if 'ZET-U' in l or 'zetu' in l.lower()]
def cmd(o, timeout=60): return r.send(X, o, timeout=timeout, connect_wait=5)
def status(): return cmd({'cmd': 'status'}, 10)
def wait_log(s, mark=0, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in blog()[mark:]: return True
        time.sleep(0.3)
    return False
def probe(line, limit=20):
    mark = len(blog())
    p = os.path.join(I, 'zetu_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l for l in blog()[mark:].splitlines() if 'zetu-probe: ' in l]
        if L: return L
        time.sleep(0.3)
    return []
def teams(**k): k['cmd'] = 'ui_teams'; return cmd(k)
def shot(out, name):
    p = os.path.join(out, name + '.png')
    if os.path.exists(p): shutil.move(p, p + '.%d.old' % time.time())
    rc = teams(action='shot', path=p); time.sleep(1.5)
    log('shot', name, 'ok' if os.path.exists(p) else ('MISSING %s' % rc))
def risks():
    mark = len(blog()); probe('risks')
    return [l.split('risk row ', 1)[1] for l in blog()[mark:].splitlines() if 'risk row ' in l]
LOW = 'Insufficient units selected'
def units(m):
    u = cmd({'cmd': 'list_units', 'map': m}).get('units') or []
    return [x for x in u if x.get('faction') == 1 and not x.get('dead')]
def sha(p): return hashlib.sha256(open(p, 'rb').read()).hexdigest()
def aside(p, out):
    """never delete: a file / folder of the copy goes to <out>/aside/"""
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))
def wait_map(m, limit=120):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            s = status()
            if s.get('map') == m and not s.get('busy'): return s
        except Exception: pass
        time.sleep(1)
    return status()


def seed_skills():
    """the seed save's player units and their active skills (SkillComponent ActiveSkills), EmergencyEscape left out"""
    d = json.load(open(SEED, encoding='utf-8')); ec = d['EntityComponentSaveData']['EntityComponents']; out = {}
    for g in d['PlayerUnitsSaveData']['SkillTrees']:
        comps = ec.get(g); comps = comps if isinstance(comps, list) else (comps or {}).get('Components', [])
        for c in comps:
            a = c.get('Data', {}).get('ActiveSkills')
            if a: out[g] = [k for k in a if k != 'SKILL_EmergencyEscape']
    return out
def casts(done, rounds=6):
    """the player's own skill order (UseSkillTargetRequested) of every seed unit in the dungeon, each of its skills, on the
    nearest hostile, after the units walked there (a skill order out of range waits); until done()"""
    sent = []; sk = seed_skills()
    for _ in range(rounds):
        alive = {u['guid'] for u in units(1)}
        todo = [(g, k) for g, ks in sk.items() if g in alive for k in ks]
        if not todo: sent.append('no seed unit alive in the dungeon'); break
        sent += probe('approach 1')[:1]
        for _ in range(10):
            d = re.findall(r'distance (\d+)', ''.join(probe('cast 1 %s %s' % todo[0])[:1]))
            if done() or (d and int(d[0]) <= 3): break
            time.sleep(3)
        for g, k in todo:
            if done(): return sent
            sent += probe(f'cast 1 {g} {k}')[:1]; time.sleep(2)
    return sent


def lease():
    lane = 'zetubie-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def install(out, cfg, probe_on=True):
    """the plugin (+ test probe) in BepInEx/plugins/<name>/, the config written (cfg None = no file: BepInEx writes it)"""
    pl = os.path.join(BIE, 'BepInEx', 'plugins')
    for n in ('ZetuSuite', 'ZetuProbe'):
        if os.path.exists(os.path.join(pl, n)): aside(os.path.join(pl, n), out)
    os.makedirs(os.path.join(pl, 'ZetuSuite'))
    shutil.copy2(os.path.join(BUILD, 'plugin', 'ZetuSuite.dll'), os.path.join(pl, 'ZetuSuite'))
    if probe_on:
        os.makedirs(os.path.join(pl, 'ZetuProbe')); shutil.copy2(os.path.join(BUILD, 'probe', 'ZetuProbe.dll'), os.path.join(pl, 'ZetuProbe'))
    c = os.path.join(BIE, 'BepInEx', 'config', 'amfi.zetu-suite.cfg')
    aside(c, out)
    if cfg is not None:
        os.makedirs(os.path.dirname(c), exist_ok=True)
        open(c, 'w').write('[Gameplay]\n\n' + ''.join(f'{k} = {v}\n\n' for k, v in cfg.items()))
    for p in (os.path.join(BIE, 'BepInEx', 'LogOutput.log'), os.path.join(BIE, 'BepInEx', 'ZetuSuite')):
        aside(p, out)


def start(window=True, dsload=None, extra_env=None):
    """the copy with Doorstop (BepInEx) + the rig control dylib (the bie_start.py recipe), hidden window or batchmode"""
    r.cap_wait(f'{X}: zetubie start')
    if r.alive(r.read_pid(X)): raise SystemExit(f'{X} already running')
    r.prepare_data(X)
    if os.path.exists(r.sockpath(X)): os.unlink(r.sockpath(X))
    home = os.path.join(I, 'home')
    inject = ([dsload] if dsload else []) + [os.path.join(BIE, 'libdoorstop.dylib')] + ([] if dsload else [RIG_DYLIB, ENVRESTORE])
    env = {'HOME': home, 'CFFIXED_USER_HOME': home, 'USER': os.environ.get('USER', ''), 'LOGNAME': os.environ.get('LOGNAME', ''),
           'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': I, 'DSRIG_FPS': '30', 'DSRIG_CUSTOM': '0',
           **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': ':'.join(inject),
           'DOORSTOP_ENABLED': '1', 'DOORSTOP_TARGET_ASSEMBLY': os.path.join(BIE, 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'),
           'DOORSTOP_CLR_RUNTIME_CORECLR_PATH': os.path.join(BIE, 'dotnet/libcoreclr.dylib'),
           'DOORSTOP_CLR_CORLIB_DIR': os.path.join(BIE, 'dotnet'), 'DOORSTOP_IGNORE_DISABLED_ENV': '0'}
    env.update(extra_env or {})
    env['DSML_INSERT'] = env['DYLD_INSERT_LIBRARIES']
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    if window: env['DSRIG_HIDEWIN'] = '1'
    else: env['DSRIG_BATCH'] = '1'; args.append('-batchmode')
    spec = {'args': args, 'env': env, 'front_before': r.front_pid() or 0, 'bg': False, 'show': False}
    open(os.path.join(I, 'launch.json'), 'w').write(json.dumps(spec))
    open(r.pidfile(X), 'w').write('')
    gp = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gp) if os.path.exists(gp) else 0
    subprocess.Popen([sys.executable, os.path.abspath(r.__file__), '_guard', X], stdin=subprocess.DEVNULL,
                     stdout=open(gp, 'ab'), stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(100):
        if r.alive(r.read_pid(X)): break
        time.sleep(0.1)
    log(f'{X}: pid {r.read_pid(X)} ({"hidden window" if window else "batchmode"}{", dsload" if dsload else ""})')
    return g0


def title(limit=900):
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=limit)


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def fresh_world(out, name_tag):
    """a throwaway copy of the seed save with a new world guid (file name + every place in the file)"""
    import uuid
    g = str(uuid.uuid4()); s = open(SEED, encoding='utf-8').read()
    s = s.replace(SEED_GUID, g).replace(SEED_GUID.replace('-', ''), g.replace('-', ''))
    s = re.sub(r'("SaveName"\s*:\s*")[^"]*(")', lambda m: m.group(1) + f'Zetu{name_tag}__AutoSave_{g.replace("-", "")}_0' + m.group(2), s, count=1)
    p = os.path.join(out, 'save_in', f'Zetu{name_tag}__AutoSave_{g.replace("-", "")}_0.json'); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w', encoding='utf-8').write(s)
    return p, g


# ------------------------------------------------------------------------------------------------------------ run
def run(out):
    S = {}
    install(out, {'skills_without_weapon': 'true', 'max_units': os.environ.get('ZETU_MAX', '8')})   # ZETU_MAX: diagnosis runs only
    for p in (os.path.join(I, 'data', 'Saves'), os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(I, 'player.log')): aside(p, out)
    mp = os.path.join(I, 'mp.json')                    # the copy's multiplayer role (another lane's): single player here, put back after
    if os.path.exists(mp): shutil.move(mp, os.path.join(out, 'mp.json.of_copy')); RESTORE.append((os.path.join(out, 'mp.json.of_copy'), mp))
    os.makedirs(os.path.join(I, 'data', 'Saves'))
    g0 = start(window=True)
    title()
    L = blog()
    S['start'] = zl(L)[:12]
    check('plugin on: settings read (skills on, max 8)', 'skills without weapon requirement on, max units in the dungeon 8' in L, S['start'])
    check('plugin on: every part patched', all(x in L for x in ('skills without weapon requirement: on', 'units in the dungeon: 8',
          'expedition UI: 8 unit panels', 'save warning: on')) and 'OFF' not in '\n'.join(zl(L)), S['start'])
    check('probe ready', 'zetu-probe: ready' in L)
    # ---- W: Cancel on a fresh world: not loaded, nothing copied
    pc, gc = fresh_world(out, 'Cancel')
    mark = len(blog())
    log('load (cancel)', cmd({'cmd': 'load_save', 'file': pc}))
    check('W: the warning shows before the load', wait_log('save warning: shown for', mark, 60), zl(blog()[mark:])[-4:])
    time.sleep(1.5)
    nt = probe('notice'); S['notice'] = nt
    check('W: the game\'s notice box shows the warning with Load / Cancel and the backup folder',
          nt and 'shown True' in nt[0] and 'ZET-U Mod Suite' in nt[0] and 'save_backups' in nt[0] and 'ok [Load]' in nt[0] and 'cancel [Cancel]' in nt[0], nt)
    shot(out, 'save_warning')
    probe('notice_cancel'); time.sleep(4)
    st = status()
    check('W: Cancel: the world is not loaded (still at the title)', st.get('scene') == 1, st)
    bk = os.path.join(BIE, 'BepInEx', 'ZetuSuite', 'save_backups')
    check('W: Cancel: nothing copied', not os.path.exists(bk) or not os.listdir(bk), os.listdir(bk) if os.path.exists(bk) else '')
    # ---- W: Load: backup first, then the world loads
    src = os.path.join(out, 'save_in', os.path.basename(SEED)); shutil.copy2(SEED, src)
    mark = len(blog())
    log('load', cmd({'cmd': 'load_save', 'file': src}))
    check('W: asked again for the seed world', wait_log('save warning: shown for', mark, 60))
    time.sleep(1.5); probe('notice_ok')
    wait_log('save warning: answered', mark, 30)
    L = blog()[mark:]
    m = re.search(r'save warning: backup of \S+ \(world ([0-9a-f-]+)\): (\d+) file\(s\) copied to (.+)', L)
    S['backup'] = m.groups() if m else zl(L)[-5:]
    ok = False
    if m:
        d = m.group(3).strip(); fs = sorted(os.listdir(d)) if os.path.isdir(d) else []
        live = os.path.join(I, 'data', 'Saves', os.path.basename(SEED))
        ok = (m.group(1) == SEED_GUID and os.path.basename(SEED) in fs and sha(os.path.join(d, os.path.basename(SEED))) == sha(SEED)
              and os.path.dirname(d) == bk and re.match(r'\d{4}-\d\d-\d\d_\d{4}_', os.path.basename(d)))
        S['backup_files'] = fs; S['live_same_before_load'] = os.path.exists(live) and sha(live) == sha(SEED)
    check('W: Load: the world\'s save copied first (same bytes) to BepInEx/ZetuSuite/save_backups/<date>_<clan>', ok, S['backup'])
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
    check('W: then the world loads', status().get('scene') == 2)
    cmd({'cmd': 'set_speed', 'speed': 1})
    S['speed_loaded'] = probe('speed')
    # ---- the seed save: 4 units in the dungeon, 3 at the base. Back to the base through the game's result screen.
    if units(1):
        log('probe', probe('result')); time.sleep(4)
        d = units(1)
        log('back to base', cmd({'cmd': 'trn_changemap', 'from': 1, 'to': 0, 'guids': ','.join(u['guid'] for u in d)}))
        wait_map(0, 120); time.sleep(3)
    base = units(0)
    for _ in range(4):
        if len(base) >= 8: break
        log('probe', probe('recruit')); time.sleep(4)
        base = units(0); log('base units', len(base))
    check('8 player units at the base', len(base) >= 8, len(base))
    g8 = [u['guid'] for u in base[:8]]
    log('party', probe('party'))
    # ---- 7: a party of 4 with max 8
    teams(action='go'); time.sleep(3)
    teams(action='stock_select', units=','.join(g8[:4])); time.sleep(2)
    teams(action='stock_next'); time.sleep(3)
    rk = risks(); S['risks_4'] = rk
    check('7: 4 of max 8 selected: the low unit count risk shows', any(LOW in x and '4' in x and '8' in x for x in rk), rk)
    teams(action='stock_close'); time.sleep(2)
    # ---- 1 / 3: region step with 8
    teams(action='go'); time.sleep(3)
    teams(action='stock_select', units=','.join(g8)); time.sleep(2)
    L = blog()
    m = re.findall(r'expedition: (\d+) selected', L)
    shot(out, 'expedition_8_select')
    S['select'] = [l for l in L.splitlines() if 'portraits:' in l or 'count text' in l][-3:]
    check('1: 8 units selected in the expedition window', m and m[-1] == '8', m[-3:])
    check('3: portraits 8 in one row, no overlap', re.search(r'portraits: 8 \(4 stock \+ 4 copies\).*, no overlap', L), S['select'])
    check('3: count text says (8/8)', re.search(r"count text now '[^']*\(8/8\)'", L), S['select'])
    # ---- 2: supplies step
    teams(action='stock_next'); time.sleep(3)
    L = blog(); shot(out, 'expedition_8_supplies')
    S['supplies'] = [l for l in L.splitlines() if 'supplies' in l][-3:]
    st = teams(action='state').get('stock') or {}
    check('2: supplies step: 8 panels in one row, no overlap', re.search(r'supplies: 8 unit panels .*, no overlap', L), S['supplies'])
    check('2: each of the 8 units has its own panel', st.get('state') == 1 and len({p for p in (st.get('panels') or []) if p}) == 8, st)
    rk = risks(); S['risks_8'] = rk
    check('7: 8 of max 8: no low unit count risk', not any(LOW in x for x in rk), rk)
    # ---- 4: into the dungeon
    log('accept', teams(action='stock_accept'))
    wait_map(1, 180); time.sleep(8)
    d = units(1); S['dungeon_units'] = len(d)
    S['speed_dungeon'] = probe('speed')
    check('4: 8 units in the dungeon', len(d) == 8, len(d))
    p0 = len(plog())
    probe('select_all'); time.sleep(2)
    for t, n, view in ((8, 'inventory', 'EntityInventoryUI'), (6, 'status', 'EntityStatusUI'), (7, 'skill', 'EntitySkillUI')):
        mark = len(blog())
        probe('toggle %d' % t); time.sleep(2.5)
        shot(out, 'dungeon_8_' + n)
        L = blog()
        fit = re.findall(view + r': (\d+) units shown, row scale ([\d.]+), x (-?\d+)\.\.(-?\d+) of (\d+) px', L)
        S['fit_' + n] = fit[-1] if fit else None
        check(f'5: {n} window: 8 panels (4 stock + 4 copies)', re.search(view + r': 8 panels \(4 stock \+ 4 copies\)', L), [l for l in L.splitlines() if view in l][-3:])
        check(f'5: {n} window shows the 8 units inside the screen', fit and fit[-1][0] == '8' and int(fit[-1][2]) >= 0 and int(fit[-1][3]) <= int(fit[-1][4]), fit[-1:])
        probe('toggle %d' % t); time.sleep(1)
    exc = [l for l in plog()[p0:].splitlines() if 'Exception' in l]
    check('5: the three windows with 8 selected: no exception', not exc, exc[:4])
    S['speed_windows'] = probe('speed')
    if os.environ.get('ZETU_HOLD'):                    # manual look: hold here until <out>/stop exists (1 h at most)
        log('HOLD: copy', X, 'in the dungeon; touch', os.path.join(out, 'stop'))
        t0 = time.time()
        while not os.path.exists(os.path.join(out, 'stop')) and time.time() - t0 < 3600: time.sleep(2)
    # ---- S: weapons off, the units cast their weapon skills
    mark = len(blog()); p0 = len(plog())
    uq = probe('unequip_all 1'); time.sleep(3)
    S['units_list'] = probe('units_list 1')            # the probe's guid list (the Windows run uses it)
    # the rig's stock_accept (InputOnAccept, confirm skipped) leaves the expedition window's speed lock + PopupTimeLock
    # (same with max_units 4, plugin party part off: out/max4, 2026-10-19): released here so the dungeon runs
    if 'lock 1' in ''.join(probe('speed')): probe('speed_unlock')
    if 'PopupTimeLock=1' in ''.join(probe('speed')): probe('speed_clear')
    cmd({'cmd': 'set_speed', 'speed': 1}); time.sleep(2)   # (3 VeryFast has no time multiplier in the game: KeyNotFoundException)
    S['speed_cast'] = probe('speed')
    S['casts'] = casts(lambda: 'durability cost with an empty slot skipped' in blog()[mark:] and 'weapon requirement skipped' in blog()[mark:])
    L = blog()[mark:]
    bp = probe('bypass'); S['bypass'] = bp
    S['weapon_lines'] = [l for l in L.splitlines() if 'weapon requirement skipped' in l or 'durability cost' in l][:8]
    check('S: weapons taken off (the game\'s unequip, 8 units)', uq and 'unequip_all 1: 8 units' in uq[0], uq)
    check('S: the game asks for a skill without its weapon and gets yes', 'weapon requirement skipped' in L, S['weapon_lines'])
    check('S: the weapon skill is cast with an empty hand (its durability cost skipped)', 'durability cost with an empty slot skipped' in L, S['weapon_lines'])
    exc = [l for l in plog()[p0:].splitlines() if 'Exception' in l]
    check('S: no exception while they cast without weapons', not exc, exc[:4])
    # ---- 6: result screen with 8
    probe('result'); time.sleep(4)
    L = blog(); shot(out, 'results_8')
    S['result'] = [l for l in L.splitlines() if 'result:' in l][-3:]
    m = re.findall(r'result: (\d+) units \+ (\d+) dead, (\d+) portraits shown', L)   # unarmed units may die in step S
    check('6: result screen: 8 portraits, no overlap, the 8 units shown', re.search(r'result: 8 portraits \(4 stock \+ 4 copies\).*, no overlap', L)
          and m and int(m[-1][0]) + int(m[-1][1]) == 8 and m[-1][2] == '8', S['result'])
    # ---- W: asked once per world: the seed world again loads with no question
    mark = len(blog())
    cmd({'cmd': 'load_save', 'name': os.path.basename(SEED)[:-5]})
    time.sleep(20)
    check('W: the same world again: no second question', 'save warning: shown' not in blog()[mark:], zl(blog()[mark:])[-3:])
    S['exceptions'] = [l for l in plog().splitlines() if 'Exception' in l][:10]
    check('no exception in player.log during the run', not S['exceptions'], S['exceptions'][:4])
    return S, g0


def fresh(out):
    """no config: BepInEx writes amfi.zetu-suite.cfg with the defaults; max 4 = no party patch"""
    install(out, None, probe_on=False)
    g0 = start(window=False); title(600)
    L = blog(); S = {'start': zl(L)[:8]}
    c = os.path.join(BIE, 'BepInEx', 'config', 'amfi.zetu-suite.cfg')
    txt = open(c).read() if os.path.exists(c) else ''
    S['cfg'] = [l for l in txt.splitlines() if '=' in l and not l.startswith('#')]
    check('fresh: config written with the defaults', 'skills_without_weapon = true' in txt and 'max_units = 4' in txt, S['cfg'])
    check('fresh: max 4 = the game\'s value: no party / screen patch', 'max units in the dungeon 4' in L and 'units in the dungeon:' not in L and 'expedition UI' not in L, S['start'])
    check('fresh: skills and save warning on', 'skills without weapon requirement: on' in L and 'save warning: on' in L, S['start'])
    return S, g0


def disable(out):
    """the mod loader (dsload) with its zetu-suite module enabled + this plugin: the plugin says OFF and patches nothing"""
    LB = os.path.join(os.path.dirname(os.path.dirname(HERE)), '..', 'loader', 'build')
    LB = os.path.abspath(LB)
    install(out, {'skills_without_weapon': 'true', 'max_units': 8}, probe_on=False)
    md = os.path.join(out, 'mods'); aside(md, out); os.makedirs(md)
    shutil.copytree(os.path.join(LB, 'mods', 'zetu-suite'), os.path.join(md, 'zetu-suite'))
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': ['zetu-suite'], 'order': ['zetu-suite'], 'settings': {'zetu-suite': {'max_units': 6}}}, open(prof, 'w'))
    aside(os.path.join(I, 'dsload.log'), out)
    g0 = start(window=False, dsload=os.path.join(LB, 'dsload.dylib'),
               extra_env={'DSL_MODS_DIR': md, 'DSL_PROFILE': prof, 'DSL_NO_ALERT': '1', 'DSL_CLOUD': '0', 'DSL_NOTCH': '0', 'DSML_DATA': os.path.join(I, 'data')})
    t0 = time.time()
    while time.time() - t0 < 600 and 'Chainloader startup complete' not in blog(): time.sleep(2)
    time.sleep(5)
    L = blog(); D = open(os.path.join(I, 'dsload.log'), errors='replace').read() if os.path.exists(os.path.join(I, 'dsload.log')) else ''
    S = {'plugin': zl(L)[:6], 'loader': [l for l in D.splitlines() if 'zetu' in l][:6]}
    check('disable: the loader\'s zetu-suite module runs (max units 6)', 'max units in the dungeon 6' in D, S['loader'])
    check('disable: the plugin logs one OFF line and patches nothing',
          "OFF, the mod loader's ZET-U Mod Suite module runs" in L and 'save warning: on' not in L and 'skills without weapon requirement: on' not in L, S['plugin'])
    return S, g0


def main():
    global X, I
    a = sys.argv[1:] or ['run']
    out = os.path.expanduser(a[1] if len(a) > 1 and a[0] in ('run', 'fresh', 'disable') else f'~/ds_zetubie/out/{a[0]}')
    if a[0] in ('cmd', 'probe'):
        X = os.environ['ZETU_X']; I = r.idir(X)
        print(json.dumps(cmd(json.loads(a[1]), 120)) if a[0] == 'cmd' else probe(a[1])); return
    os.makedirs(out, exist_ok=True)
    lane, pair = lease(); X = pair[0]; I = r.idir(X); log('lease', pair, 'copy', X)
    S = {}; g0 = 0
    try:
        S, g0 = {'run': run, 'fresh': fresh, 'disable': disable}[a[0]](out)
    except Exception as e:
        import traceback; traceback.print_exc()
        check('run finished', False, repr(e))
    finally:
        down()
        for k, o in RESTORE:
            if os.path.exists(k) and not os.path.exists(o): shutil.move(k, o)
        for f in ('player.log', 'guard.log', 'dsload.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(out, f))
        if os.path.exists(os.path.join(BIE, 'BepInEx', 'LogOutput.log')): shutil.copy2(os.path.join(BIE, 'BepInEx', 'LogOutput.log'), os.path.join(out, 'LogOutput.log'))
        g = open(os.path.join(I, 'guard.log'), errors='replace').read()[g0:] if os.path.exists(os.path.join(I, 'guard.log')) else ''
        check('guard: never a visible window, never frontmost', 'GUARD KILLED' not in g, g[-300:])
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
        S['checks'] = checks; json.dump(S, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    print(f'\n{len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
