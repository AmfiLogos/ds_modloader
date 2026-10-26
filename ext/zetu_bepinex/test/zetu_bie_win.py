#!/usr/bin/env python3
"""ZET-U Mod Suite with PLAIN BepInEx be.788 on the Windows test VM (ds_multiplayer VMWARE.md rules), through the winbot
agent (winrig.py, ~/ds_winbot/ds_multiplayer/win/winbot or WINBOT_DIR).

The game is a COPY of the Steam install in C:\\dsm\\zetubie\\game (the Steam install is never changed); be.788 is unzipped
into the copy, the plugin + the test probe go to BepInEx\\plugins\\<name>\\, max_units = 8. The probe (ZETU_TEST_DATA) points
Application.persistentDataPath at C:\\dsm\\zetubie\\data, so the game's saves and settings never touch LocalLow (Steam
Cloud); -logFile keeps Player.log there too. Throwaway saves only (fresh world guids). The owner's LocalLow + Steam
userdata\\<id>\\2798330 are hashed before and compared after (owner_backup.py).

  zetu_bie_win.py run [out]     the checks below; the VM must already run (started visibly by the caller)
  zetu_bie_win.py console [out] C: the BepInEx console window (owner rule: never): two starts, BepInEx.cfg console on
                                before the first; the window BepInEx opens is closed by the plugin, the second start opens
                                none, LogOutput.log is written both times

Real breaks the run catches (Windows: the 1-byte bool ABI class of the v0.1.15 ZET-U hang, x64 Harmony patches):
 L  the plugin loads with plain be.788 and every part is patched (no part OFF)
 N  the world loads with no question and writes no backup folder (0.1.2: no save warning)
 P  the game's party cap reads 8
 5  Inventory / Status / Skill windows: 8 panels (4 stock + 4 copies), the shown units inside the screen, no exception
 S  full-HP units from the base into the dungeon, weapons taken off there, the player's own skill order for a FireStaff
    skill: bypass asked + answered, the cast runs with an empty hand (durability cost skipped), no exception, the game
    keeps running (no hang)
 O  the owner's LocalLow and Steam userdata files are unchanged"""
import base64, json, os, re, subprocess, sys, time, uuid
H = os.path.expanduser('~')
WB = os.environ.get('WINBOT_DIR') or f'{H}/ds_winbot/ds_multiplayer/win/winbot'
sys.path.insert(0, WB)
import winrig as W                                                   # noqa: E402
import owner_backup as OB                                            # noqa: E402
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get('ZETU_BUILD') or os.path.join(os.path.dirname(HERE), 'build')
BE788 = os.environ.get('ZETU_BE788_ZIP') or f'{H}/ds_zetubie/win/be788_win_x64.zip'
SEED = f'{H}/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'
SEED_GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
WK = os.environ.get('ZETU_WIN_WK') or r'C:\dsm\zetubie'; GAME = WK + r'\game'; DATA = WK + r'\data'   # ZETU_WIN_WK: a lane's own folder
STEAM_GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Dungeon Settlers'
BLOG = GAME + r'\BepInEx\LogOutput.log'
checks = []; w = None; PID = None; out_dir = None; BK = None


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def run(exe, args, wait=600, cwd=WK):
    return w.a('run', exe=exe, args=args, cwd=w.expand(cwd), wait_ms=wait * 1000, capture=True)
def blog(): return w.text(BLOG) or ''
def plog(): return w.text(WK + r'\player.log') or ''
def zl(L): return [l for l in L.splitlines() if 'ZET-U' in l or 'zetu' in l.lower()]
def probe(line, limit=20):
    mark = len(blog())
    w.write(WK + r'\zetu_probe_req.txt', line + '\n')
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l for l in blog()[mark:].splitlines() if 'zetu-probe: ' in l]
        if L: return L
        time.sleep(0.4)
    return []
def shot(out, name):
    try: w.shot(os.path.join(out, name + '.png')); log('shot', name)
    except Exception as e: log('shot', name, 'failed', e)


# EmergencyEscape: no target. WaterDive: the stock game throws NullReferenceException in its MoveRoutine / AttackRoutine
# when ordered this way, with the weapons ON and the bypass never asked (2026-10-19, out/hold4): not this plugin's.
SKIP_SKILLS = ('SKILL_EmergencyEscape', 'SKILL_WaterDive')
def seed_skills():
    """the seed save's player units and their active skills (SkillComponent ActiveSkills), SKIP_SKILLS left out"""
    d = json.load(open(SEED, encoding='utf-8')); ec = d['EntityComponentSaveData']['EntityComponents']; out = {}
    for g in d['PlayerUnitsSaveData']['SkillTrees']:
        comps = ec.get(g); comps = comps if isinstance(comps, list) else (comps or {}).get('Components', [])
        for c in comps:
            a = c.get('Data', {}).get('ActiveSkills')
            if a: out[g] = [k for k in a if k not in SKIP_SKILLS]
    return out
def units_in(m):
    """guids of the live player units on map m (probe 'units_list')"""
    r = probe(f'units_list {m}')
    return set(re.findall(r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', r[0])) if r else set()


def fresh_world(out, tag):
    g = str(uuid.uuid4()); s = open(SEED, encoding='utf-8').read()
    s = s.replace(SEED_GUID, g).replace(SEED_GUID.replace('-', ''), g.replace('-', ''))
    name = f'Zetu{tag}__AutoSave_{g.replace("-", "")}_0'
    s = re.sub(r'("SaveName"\s*:\s*")[^"]*(")', lambda m: m.group(1) + name + m.group(2), s, count=1)
    p = os.path.join(out, 'save_in', name + '.json'); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w', encoding='utf-8').write(s)
    return p, name, g


def setup(out):
    """the game copy (robocopy, Steam install unchanged), be.788, plugin + probe, config, test data folder"""
    w.write(WK + r'\.keep', b'')
    src = w.a('stat', path=STEAM_GAME + r'\GameAssembly.dll'); dst = w.a('stat', path=GAME + r'\GameAssembly.dll')
    if not dst.get('exists') or dst.get('size') != src.get('size') or dst.get('mtime') != src.get('mtime'):
        r = run(r'C:\Windows\System32\Robocopy.exe', f'"{STEAM_GAME}" "{GAME}" /E /COPY:DAT /R:1 /W:1 /NP /NFL /NDL '
                '/XD BepInEx dotnet /XF winhttp.dll version.dll doorstop_config.ini .doorstop_version', 1200)
        if r.get('exit', 99) >= 8: raise SystemExit('robocopy: ' + r.get('out', '')[-300:])
        log('game copied to', GAME)
    if not w.exists(GAME + r'\winhttp.dll'):
        w.upload(BE788, WK + r'\be788.zip')
        r = run(r'C:\Windows\System32\tar.exe', f'-xf "{WK}\\be788.zip" -C "{GAME}"', 300)
        if not w.exists(GAME + r'\BepInEx\core\BepInEx.Unity.IL2CPP.dll'): raise SystemExit('be.788 unzip failed: ' + r.get('out', '')[-300:])
        log('be.788 unzipped into the copy')
    w.write(GAME + r'\steam_appid.txt', '2798330')
    if not w.exists(GAME + r'\BepInEx\config\BepInEx.cfg'):
        w.write(GAME + r'\BepInEx\config\BepInEx.cfg', '[Logging.Disk]\n\nInstantFlushing = true\n')
    park = WK + r'\old\run_' + time.strftime('%Y%m%d-%H%M%S')
    for p in (BLOG, WK + r'\player.log', GAME + r'\BepInEx\ZetuSuite', DATA, GAME + r'\BepInEx\config\amfi.zetu-suite.cfg'):
        if w.exists(p): w.move(p, park + '\\' + p.rsplit('\\', 1)[1])
    w.put(os.path.join(BUILD, 'plugin', 'ZetuSuite.dll'), GAME + r'\BepInEx\plugins\ZetuSuite\ZetuSuite.dll')
    w.put(os.path.join(BUILD, 'probe', 'ZetuProbe.dll'), GAME + r'\BepInEx\plugins\ZetuProbe\ZetuProbe.dll')
    w.write(GAME + r'\BepInEx\config\amfi.zetu-suite.cfg', '[Gameplay]\n\nskills_without_weapon = true\n\nmax_units = 8\n')
    us = json.loads(w.read(r'%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers\UserSetting.json')[0].decode('utf-8-sig'))   # read only
    g = us['GraphicSettingData']                               # VMWARE.md rule 3: windowed 1728x1000, never full screen
    g['SaveCurrentScreenType'] = 1
    for a_, b_ in (('ResolutionWidth', 'ResolutionHeight'), ('WindowedResolutionWidth', 'WindowedResolutionHeight')): g[a_], g[b_] = 1728, 1000
    for k in list(us.get('SoundSettingData', {})):             # owner: no sound in tests
        if k.endswith('Percent'): us['SoundSettingData'][k] = 0.0
        if k.endswith('Mute'): us['SoundSettingData'][k] = True
    us.setdefault('GeneralSettingData', {})['MoveScreenByEdgeScreen'] = False   # owner: no edge scrolling in tests
    w.write(DATA + r'\UserSetting.json', json.dumps(us, indent=1))
    saves = {}
    for tag in ('Load',):
        p, name, g = fresh_world(out, tag)
        w.upload(p, DATA + '\\Saves\\' + name + '.json'); saves[tag] = (p, name, g)
    return saves


def start(timeout=900, data=DATA):
    if not w.screen_mode_ok(): raise SystemExit('screen mode is not windowed (3): not started')
    if w.procs(): raise SystemExit('another DungeonSettlers.exe runs in the VM (another lane): not started')   # never killed by us
    env = [f'ZETU_TEST_DATA={data}', f'DSRIG_DIR={WK}', 'SteamAppId=2798330', 'SteamGameId=2798330']
    r = w.a('run', exe=GAME + r'\DungeonSettlers.exe', args=f'-logFile "{WK}\\player.log"', cwd=GAME, env=env, show=True, wait_ms=0)
    global PID; PID = r.get('pid'); log('game pid', PID)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if 'Chainloader startup complete' in blog(): break
        if not w.procs(): raise SystemExit('the game ended before BepInEx was ready: ' + blog()[-400:])
        time.sleep(3)
    ok = 0
    while time.time() - t0 < timeout:                       # the title: MainFlow there in 3 answers in a row (the
        sc = probe('scene', 10)                              # entry scene has one for a moment, then the title loads)
        ok = ok + 1 if sc and 'mainflow True' in sc[0] else 0
        if ok >= 3: return sc[0]
        time.sleep(3)
    raise SystemExit('no title in %d s' % timeout)


# the weapon skill cast in step S: a seed unit at the BASE (full HP; the seed's dungeon units are near death: all 4 died
# before a cast, Windows run 2026-10-26). FireCloak asks Equipment_FireStaff; with the hand emptied in the dungeon it gave
# the bypass + the empty-slot skip on Mac and on Windows (2026-10-26). Water skills (SummonBubbling, WaterDive: the stock
# game throws NullReferenceException when ordered so), EmergencyEscape (no target) and tile-target skills (traps.md 8,
# e.g. DestructiveSplit) are never cast.
WEAPON_CASTS = {'60c78b46-2bec-4a71-9db2-fbd10a4e28ab': ['SKILL_FireCloak']}                                        # FireStaff
def running():
    """the game runs (not paused); a window's speed lock / PopupTimeLock is released, the result screen never opened"""
    sp = ''.join(probe('speed'))
    if 'speed: Pause' in sp or 'lock 1' in sp:
        if 'lock 1' in sp: probe('speed_unlock')
        if 'PopupTimeLock=1' in sp: probe('speed_clear')
        probe('set_speed 1'); time.sleep(1); sp = ''.join(probe('speed'))
    return 'speed: Normal' in sp, sp


def notices(n=4):
    """answer the game's confirmation boxes with their OK button (the player's click), up to n; their texts"""
    seen = []
    for _ in range(n):
        time.sleep(1.5)
        nt = ''.join(probe('notice'))
        if 'shown True' not in nt: break
        seen.append(nt[:300]); probe('notice_ok')
    return seen
def expedition(S):
    """8 full-HP units from the base (recruits fill it up, the WEAPON_CASTS units first) into the dungeon through the game's
    expedition window (probe exped_*); True = 8 there"""
    for _ in range(6):
        if len(units_in(0)) >= 8: break
        probe('recruit'); time.sleep(5)
    base = sorted(units_in(0)); cs = [g for g in WEAPON_CASTS if g in base]
    g8 = (cs + [g for g in base if g not in cs])[:8]; S['g8'] = g8
    probe('exped_open'); time.sleep(4)
    if not re.search(r'region [1-9]', ''.join(probe('exped_state'))): probe('exped_region 1'); time.sleep(1)
    S['exped'] = probe('exped_select ' + ','.join(g8))[:1]; time.sleep(2)
    probe('exped_next'); time.sleep(3)
    probe('exped_start'); S['start_notices'] = notices()
    t0 = time.time()
    while time.time() - t0 < 300 and len(units_in(1)) < 8: time.sleep(3)
    S['dungeon_units'] = len(units_in(1)); log('expedition', len(g8), 'selected,', S['dungeon_units'], 'in the dungeon')
    time.sleep(8); running()
    return S['dungeon_units'] == 8


def weapon_step(S, out):
    """S: weapons off in the dungeon right before the casts (unequip_all 1; an unequip at the base left the hand full on
    Windows), then the WEAPON_CASTS skill by its unit, while the game runs.
    Pass: the bypass asked and answered yes, and at least 2 casts with an empty hand (durability cost skipped)."""
    mark = len(blog()); p0 = len(plog())
    uq = probe('unequip_all 1'); S['unequip'] = uq; time.sleep(4)
    S['casts'] = []; S['paused'] = []
    empty = lambda: blog()[mark:].count('durability cost with an empty slot skipped')
    for r in range(15):
        if empty() >= 2: break
        alive = units_in(1)
        todo = [(g, ks[r % len(ks)]) for g, ks in WEAPON_CASTS.items() if g in alive]
        if not todo: S['casts'].append('no weapon-skill unit alive in the dungeon'); break
        ok, sp = running()
        if not ok: S['paused'].append(sp[-120:]); time.sleep(2); continue
        if r % 3 == 0: S['casts'] += probe('approach 1')[:1]; time.sleep(10)     # walk first: a cast order stops the walk
        for g, k in todo: S['casts'] += probe(f'cast 1 {g} {k}')[:1]
        time.sleep(6)
    L = blog()[mark:]
    S['weapon_lines'] = [l for l in L.splitlines() if 'weapon requirement skipped' in l or 'durability cost' in l][:8]
    S['bypass'] = probe('bypass'); S['empty_casts'] = empty()
    check('S: weapons taken off (the game\'s unequip)', uq and re.search(r'unequip_all 1: [1-9] units', uq[0]), uq)
    # the plugin logs its first 5 bypass answers only, and the expedition (FireCloak's unit without a FireStaff) can use them
    # up before this step: the log line is read from the whole log, the yes count from the probe
    check('S: the game asks for a skill without its weapon and gets yes', 'weapon requirement skipped' in blog() and re.search(r'bypass: yes [1-9]', ''.join(S['bypass'])), S['weapon_lines'] + S['bypass'])
    check('S: at least 2 weapon skills cast with an empty hand (durability cost skipped)', empty() >= 2, [empty()] + S['paused'][:3])
    sc = probe('scene', 10)
    check('S: the game still answers (no hang)', bool(sc), sc)
    exc = [l for l in plog()[p0:].splitlines() if 'Exception' in l]
    check('S: no exception while they cast without weapons', not exc, exc[:4])
    shot(out, 'win_dungeon_no_weapons')


def main_run(out):
    S = {}
    log('owner data backup'); t_bk = OB.backup(w); S['owner_backup'] = t_bk
    saves = setup(out)
    S['title'] = start(); time.sleep(5)
    L = blog(); S['start'] = zl(L)[:14]
    check('L: plain be.788 loads the plugin; settings read (skills on, max 8)', 'skills without weapon requirement on, max units in the dungeon 8' in L, S['start'])
    check('L: every part patched, none OFF', all(x in L for x in ('skills without weapon requirement: on', 'units in the dungeon: 8',
          'expedition UI: 8 unit panels')) and 'OFF' not in '\n'.join(zl(L)), S['start'])
    shot(out, 'win_title')
    # ---- N: the world loads with no question and writes no backup folder (0.1.2: no save warning)
    mark = len(blog())
    for _ in range(10):                                    # until the title's event queue takes it
        if 'sent' in ''.join(probe('load ' + saves['Load'][1])): break
        time.sleep(3)
    t0 = time.time(); u = []
    while time.time() - t0 < 300:
        u = probe('units', 10)
        if u and re.search(r'Dungeon [1-9]', u[0]): break
        time.sleep(5)
    S['units'] = u
    nt = probe('notice'); S['notice'] = nt
    check('N: the world loads with no question (4 units in the dungeon, "Load campaign complete", no notice box, no "save warning" line)',
          u and 'Dungeon 4' in u[0] and 'Load campaign complete' in plog() and 'save warning' not in blog()[mark:] and nt and 'shown False' in nt[0], [u, nt])
    bk = GAME + r'\BepInEx\ZetuSuite'
    check('N: no backup folder written (BepInEx\\ZetuSuite)', not w.exists(bk), w.a('ls', path=bk).get('entries') if w.exists(bk) else '')
    time.sleep(10)
    pp = probe('party'); S['party'] = pp
    check('P: the game\'s party cap reads 8', pp and 'PlayerPartyMax 8' in pp[0], pp)
    # ---- 8 full-HP units from the base into the dungeon (the seed's dungeon units are near death)
    expedition(S)
    # ---- 5: the three windows with the 8 selected (before S: the unarmed units may fall during the casts)
    p0 = len(plog())
    probe('select_all'); time.sleep(2)
    for t, n, view in ((8, 'inventory', 'EntityInventoryUI'), (6, 'status', 'EntityStatusUI'), (7, 'skill', 'EntitySkillUI')):
        probe('toggle %d' % t); time.sleep(3)
        shot(out, 'win_dungeon_' + n)
        L = blog()
        fit = re.findall(view + r': (\d+) units shown, row scale ([\d.]+), x (-?\d+)\.\.(-?\d+) of (\d+) px', L)
        S['fit_' + n] = fit[-1] if fit else None
        check(f'5: {n} window: 8 panels (4 stock + 4 copies)', re.search(view + r': 8 panels \(4 stock \+ 4 copies\)', L), [l for l in L.splitlines() if view in l][-3:])
        check(f'5: {n} window: the shown units inside the screen', fit and int(fit[-1][2]) >= 0 and int(fit[-1][3]) <= int(fit[-1][4]), fit[-1:])
        probe('toggle %d' % t); time.sleep(1)
    exc = [l for l in plog()[p0:].splitlines() if 'Exception' in l]
    check('5: the three windows: no exception', not exc, exc[:4])
    # ---- S: weapons off, the FireStaff skill with an empty hand
    weapon_step(S, out)
    S['exceptions'] = [l for l in plog().splitlines() if 'Exception' in l][:10]
    check('no exception in Player.log during the run', not S['exceptions'], S['exceptions'][:4])
    return S


CONSOLE_HOSTS = ('conhost.exe', 'openconsole.exe', 'windowsterminal.exe')
def console_windows(base):
    """visible windows of a console: a BepInEx title, or a console host process that was not there before the start"""
    hits = []
    for p in w.a('ps').get('procs', []):
        for v in p.get('windows', []):
            t = v.get('title', '')
            if 'BepInEx' in t or (p.get('name', '').lower() in CONSOLE_HOSTS and p.get('pid') not in base): hits.append(f"{p.get('name')} {p.get('pid')}: {t}")
    return hits
def set_console(on):
    """BepInEx.cfg [Logging.Console] Enabled = on (the player's file as BepInEx writes it); every other line kept"""
    c = GAME + r'\BepInEx\config\BepInEx.cfg'; t = w.text(c) or ''
    i = t.find('[Logging.Console]')
    if i < 0: t += '\n[Logging.Console]\n\nEnabled = %s\n' % ('true' if on else 'false')
    else:
        j = t.find('\n[', i + 1); j = len(t) if j < 0 else j
        sec = re.sub(r'(?m)^Enabled = \w+', 'Enabled = %s' % ('true' if on else 'false'), t[i:j]) if re.search(r'(?m)^Enabled = ', t[i:j]) \
            else t[i:j].replace('[Logging.Console]', '[Logging.Console]\nEnabled = %s' % ('true' if on else 'false'), 1)
        t = t[:i] + sec + t[j:]
    w.write(c, t)
def console_cfg():
    t = w.text(GAME + r'\BepInEx\config\BepInEx.cfg') or ''; i = t.find('[Logging.Console]'); j = t.find('\n[', i + 1)
    con = t[i:j if j > 0 else len(t)] if i >= 0 else ''; k = t.find('[Logging.Disk]'); m = t.find('\n[', k + 1)
    disk = t[k:m if m > 0 else len(t)] if k >= 0 else ''
    return re.findall(r'(?m)^Enabled = (\w+)', con), re.findall(r'(?m)^Enabled = (\w+)', disk)
def watch_start(tag, after=10, timeout=600):
    """start the copy (windowed, test data), poll the console windows until BepInEx's chainloader is done + after s, kill"""
    global PID
    if not w.screen_mode_ok(): raise SystemExit('screen mode is not windowed (3): not started')
    if w.procs(): raise SystemExit('another DungeonSettlers.exe runs in the VM (another lane): not started')
    park = WK + r'\old\console_' + time.strftime('%Y%m%d-%H%M%S')
    if w.exists(BLOG): w.move(BLOG, park + r'\LogOutput.log')
    base = {p.get('pid') for p in w.a('ps').get('procs', []) if p.get('name', '').lower() in CONSOLE_HOSTS}
    env = [f'ZETU_TEST_DATA={DATA}', f'DSRIG_DIR={WK}', 'SteamAppId=2798330', 'SteamGameId=2798330']
    PID = w.a('run', exe=GAME + r'\DungeonSettlers.exe', args=f'-logFile "{WK}\\player.log"', cwd=GAME, env=env, show=True, wait_ms=0).get('pid')
    log(tag, 'game pid', PID)
    seen, t0, done = [], time.time(), None
    while time.time() - t0 < timeout:
        for h in console_windows(base):
            if h not in seen: seen.append(h); log(tag, 'console window:', h)
        if done is None and 'Chainloader startup complete' in blog(): done = time.time()
        if done and time.time() - done > after: break
        if not w.procs(): break
        time.sleep(0.3)
    last = console_windows(base); L = blog()
    w.get(BLOG, os.path.join(out_dir, f'LogOutput_{tag}.log'))
    w.a('kill', pid=PID); PID = None; time.sleep(3)
    return {'seen': seen, 'after': last, 'ready': done is not None, 'log_lines': L.count('\n'),
            'console_line': [l for l in L.splitlines() if 'BepInEx console' in l]}
def console_run(out):
    global BK
    S = {}
    log('owner data backup'); BK = S['owner_backup'] = OB.backup(w)
    setup(out); set_console(True)
    S['cfg_before'] = console_cfg()
    a = S['start1'] = watch_start('start1')
    check('C: start 1 (BepInEx.cfg console on): the window BepInEx opened is seen, so the watch works', a['seen'], a)
    check('C: start 1: the plugin closed it (no console window once the plugins are loaded)', a['ready'] and not a['after'], a)
    S['cfg_after1'] = console_cfg()
    check('C: start 1: BepInEx.cfg now says console off, disk log on', S['cfg_after1'] == (['false'], ['true']), S['cfg_after1'])
    check('C: start 1: LogOutput.log written, with the plugin\'s line', a['log_lines'] > 10 and any('closed, off in BepInEx.cfg' in l for l in a['console_line']), a['console_line'])
    b = S['start2'] = watch_start('start2')
    check('C: start 2: no console window at any time', b['ready'] and not b['seen'], b)
    check('C: start 2: LogOutput.log written', b['log_lines'] > 10 and any('BepInEx console: off' in l for l in b['console_line']), b['console_line'])
    return S


def main():
    global w
    a = sys.argv[1:] or ['run']
    out = os.path.expanduser(a[1] if len(a) > 1 else '~/ds_zetubie/out/win')
    os.makedirs(out, exist_ok=True)
    global out_dir; out_dir = out
    w = W.Win(); w.up()
    S = {}
    try:
        S = {'console': console_run}.get(a[0], main_run)(out)
    except BaseException as e:
        import traceback; traceback.print_exc()
        check('run finished', False, repr(e))
    finally:
        try:
            if PID: w.a('kill', pid=PID)                       # only our own copy
        except Exception as e: log('kill:', e)
        for p, n in ((BLOG, 'LogOutput.log'), (WK + r'\player.log', 'player.log')):
            try: w.get(p, os.path.join(out, n))
            except Exception as e: log('log', n, e)
        if S.get('owner_backup') or BK:
            c = OB.compare(w, S.get('owner_backup') or BK); S['owner_compare'] = c
            check('O: the owner\'s LocalLow + Steam userdata files unchanged, no pending cloud upload', not c['diff'] and not c['remotecache_syncstate3'], c)
        S['checks'] = checks; json.dump(S, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    print(f'\n{len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
