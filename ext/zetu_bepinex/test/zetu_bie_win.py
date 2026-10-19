#!/usr/bin/env python3
"""ZET-U Mod Suite with PLAIN BepInEx be.788 on the Windows test VM (ds_multiplayer VMWARE.md rules), through the winbot
agent (winrig.py, ~/ds_winbot/ds_multiplayer/win/winbot or WINBOT_DIR).

The game is a COPY of the Steam install in C:\\dsm\\zetubie\\game (the Steam install is never changed); be.788 is unzipped
into the copy, the plugin + the test probe go to BepInEx\\plugins\\<name>\\, max_units = 8. The probe (ZETU_TEST_DATA) points
Application.persistentDataPath at C:\\dsm\\zetubie\\data, so the game's saves and settings never touch LocalLow (Steam
Cloud); -logFile keeps Player.log there too. Throwaway saves only (fresh world guids). The owner's LocalLow + Steam
userdata\\<id>\\2798330 are hashed before and compared after (owner_backup.py).

  zetu_bie_win.py run [out]     the checks below; the VM must already run (started visibly by the caller)

Real breaks the run catches (Windows: the 1-byte bool ABI class of the v0.1.15 ZET-U hang, x64 Harmony patches):
 L  the plugin loads with plain be.788 and every part is patched (no part OFF)
 W  Cancel loads nothing and copies nothing; Load copies the save first (same bytes), then the world loads; asked once
 P  the game's party cap reads 8
 5  Inventory / Status / Skill windows: 8 panels (4 stock + 4 copies), the shown units inside the screen, no exception
 S  weapons taken off in the dungeon, the player's own skill order for Greatsword / FireStaff skills: bypass asked +
    answered, the cast runs with an empty hand (durability cost skipped), no exception, the game keeps running (no hang)
 O  the owner's LocalLow and Steam userdata files are unchanged"""
import base64, hashlib, json, os, re, subprocess, sys, time, uuid
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
WK = r'C:\dsm\zetubie'; GAME = WK + r'\game'; DATA = WK + r'\data'
STEAM_GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Dungeon Settlers'
BLOG = GAME + r'\BepInEx\LogOutput.log'
checks = []; w = None; PID = None


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def run(exe, args, wait=600, cwd=WK):
    return w.a('run', exe=exe, args=args, cwd=w.expand(cwd), wait_ms=wait * 1000, capture=True)
def blog(): return w.text(BLOG) or ''
def plog(): return w.text(WK + r'\player.log') or ''
def zl(L): return [l for l in L.splitlines() if 'ZET-U' in l or 'zetu' in l.lower()]
def wait_log(s, mark=0, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in blog()[mark:]: return True
        time.sleep(0.5)
    return False
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
def sha_guest(p):
    d, _ = w.read(p, max=64 << 20); return hashlib.sha256(d).hexdigest()


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
    w.write(DATA + r'\UserSetting.json', json.dumps(us, indent=1))
    saves = {}
    for tag in ('Cancel', 'Load'):
        p, name, g = fresh_world(out, tag)
        w.upload(p, DATA + '\\Saves\\' + name + '.json'); saves[tag] = (p, name, g)
    return saves


def start(timeout=900):
    if not w.screen_mode_ok(): raise SystemExit('screen mode is not windowed (3): not started')
    if w.procs(): raise SystemExit('another DungeonSettlers.exe runs in the VM (another lane): not started')   # never killed by us
    env = [f'ZETU_TEST_DATA={DATA}', f'DSRIG_DIR={WK}', 'SteamAppId=2798330', 'SteamGameId=2798330']
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


# the weapon skills cast in step S (seed units; keys seen asking Equipment_Greatsword / Equipment_FireStaff on Mac runs 7-11).
# Water skills (SummonBubbling, WaterDive: the stock game throws NullReferenceException when ordered so) and
# EmergencyEscape (no target) are never cast.
WEAPON_CASTS = {'477ad25a-7c22-43f5-be28-db055895297e': ['SKILL_WhirlwindSlash', 'SKILL_Slay'],                      # Greatsword
                '137a4264-054d-4b21-81f2-a69d349a3bd0': ['SKILL_FireMissile', 'SKILL_Incinerate', 'SKILL_Explosion'],  # FireStaff
                '92be08ca-b7de-497a-a4ea-9aac4be0b603': ['SKILL_Incinerate', 'SKILL_Explosion', 'SKILL_BlazingEcho']}   # FireStaff
def running():
    """the game runs (not paused); a window's speed lock / PopupTimeLock is released, the result screen never opened"""
    sp = ''.join(probe('speed'))
    if 'speed: Pause' in sp or 'lock 1' in sp:
        if 'lock 1' in sp: probe('speed_unlock')
        if 'PopupTimeLock=1' in sp: probe('speed_clear')
        probe('set_speed 1'); time.sleep(1); sp = ''.join(probe('speed'))
    return 'speed: Normal' in sp, sp


def weapon_step(S, out):
    """S: weapons off, then only Greatsword / FireStaff skills, by live units next to their target, while the game runs.
    Pass: the bypass asked and answered yes, and at least 2 casts with an empty hand (durability cost skipped)."""
    mark = len(blog()); p0 = len(plog())
    uq = probe('unequip_all 1'); S['unequip'] = uq; time.sleep(2)
    S['casts'] = []; S['paused'] = []
    empty = lambda: blog()[mark:].count('durability cost with an empty slot skipped')
    for r in range(20):
        if empty() >= 2: break
        alive = units_in(1)
        todo = [(g, ks[r % len(ks)]) for g, ks in WEAPON_CASTS.items() if g in alive]
        if not todo: S['casts'].append('no weapon-skill unit alive in the dungeon'); break
        ok, sp = running()
        if not ok: S['paused'].append(sp[-120:]); time.sleep(2); continue
        g0, k0 = todo[0]                                        # walk next to the target first (an order out of range waits)
        S['casts'] += probe('approach 1')[:1]
        for _ in range(10):
            d = re.findall(r'distance (\d+)', ''.join(probe(f'cast 1 {g0} {k0}')[:1]))
            if d and int(d[0]) <= 3: break
            time.sleep(3)
        for g, k in todo:
            ok, sp = running()
            if not ok: S['paused'].append(sp[-120:]); continue
            S['casts'] += probe(f'cast 1 {g} {k}')[:1]
        for _ in range(6):
            if empty() >= 2: break
            time.sleep(1)
    L = blog()[mark:]
    S['weapon_lines'] = [l for l in L.splitlines() if 'weapon requirement skipped' in l or 'durability cost' in l][:8]
    S['bypass'] = probe('bypass'); S['empty_casts'] = empty()
    check('S: weapons taken off (the game\'s unequip)', uq and re.search(r'unequip_all 1: [1-9] units', uq[0]), uq)
    check('S: the game asks for a skill without its weapon and gets yes', 'weapon requirement skipped' in L and re.search(r'bypass: yes [1-9]', ''.join(S['bypass'])), S['weapon_lines'] + S['bypass'])
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
          'expedition UI: 8 unit panels', 'save warning: on')) and 'OFF' not in '\n'.join(zl(L)), S['start'])
    shot(out, 'win_title')
    # ---- W: Cancel
    mark = len(blog())
    for _ in range(10):                                    # until the title's event queue takes it
        if 'sent' in ''.join(probe('load ' + saves['Cancel'][1])): break
        time.sleep(3)
    check('W: the warning shows before the load', wait_log('save warning: shown for', mark, 60), zl(blog()[mark:])[-4:])
    time.sleep(2)
    nt = probe('notice'); S['notice'] = nt
    check('W: the notice box: the warning, Load / Cancel, the backup folder',
          nt and 'shown True' in nt[0] and 'ZET-U Mod Suite' in nt[0] and 'save_backups' in nt[0] and 'ok [Load]' in nt[0] and 'cancel [Cancel]' in nt[0], nt)
    shot(out, 'win_save_warning')
    probe('notice_cancel'); time.sleep(5)
    u = probe('units'); S['units_after_cancel'] = u
    bk = GAME + r'\BepInEx\ZetuSuite\save_backups'
    check('W: Cancel: nothing copied', not w.exists(bk) or not w.a('ls', path=bk).get('entries'), w.a('ls', path=bk).get('entries') if w.exists(bk) else '')
    check('W: Cancel: no world loaded', u and 'Dungeon -1' in u[0] or (u and 'Dungeon 0' in u[0]), u)
    # ---- W: Load
    mark = len(blog())
    probe('load ' + saves['Load'][1])
    check('W: asked for the second world', wait_log('save warning: shown for', mark, 60))
    time.sleep(2); probe('notice_ok')
    wait_log('save warning: answered', mark, 60)
    L = blog()[mark:]
    m = re.search(r'save warning: backup of \S+ \(world ([0-9a-f-]+)\): (\d+) file\(s\) copied to (.+)', L)
    ok = False
    if m:
        d = m.group(3).strip(); S['backup_dir'] = d
        f = d + '\\' + saves['Load'][1] + '.json'
        ok = m.group(1) == saves['Load'][2] and w.exists(f) and sha_guest(f) == hashlib.sha256(open(saves['Load'][0], 'rb').read()).hexdigest() \
            and d.lower().startswith(bk.lower() + '\\') and re.search(r'\\\d{4}-\d\d-\d\d_\d{4}_[^\\]+$', d)
    check('W: Load: the save copied first (same bytes) to BepInEx\\ZetuSuite\\save_backups\\<date>_<clan>', ok, m.groups() if m else zl(L)[-5:])
    t0 = time.time(); u = []
    while time.time() - t0 < 300:
        u = probe('units', 10)
        if u and re.search(r'Dungeon [1-9]', u[0]): break
        time.sleep(5)
    S['units'] = u
    check('W: then the world loads (4 units in the dungeon)', u and 'Dungeon 4' in u[0], u)
    time.sleep(10)
    pp = probe('party'); S['party'] = pp
    check('P: the game\'s party cap reads 8', pp and 'PlayerPartyMax 8' in pp[0], pp)
    # ---- S first, while the seed units are fresh (no window opened yet)
    weapon_step(S, out)
    # ---- 5: the three windows
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
    # ---- W: asked once per world
    mark = len(blog())
    probe('load ' + saves['Load'][1]); time.sleep(20)
    check('W: the same world again: no second question', 'save warning: shown' not in blog()[mark:], zl(blog()[mark:])[-3:])
    S['exceptions'] = [l for l in plog().splitlines() if 'Exception' in l][:10]
    check('no exception in Player.log during the run', not S['exceptions'], S['exceptions'][:4])
    return S


def main():
    global w
    a = sys.argv[1:] or ['run']
    out = os.path.expanduser(a[1] if len(a) > 1 else '~/ds_zetubie/out/win')
    os.makedirs(out, exist_ok=True)
    w = W.Win(); w.up()
    S = {}
    try:
        S = main_run(out)
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
        if S.get('owner_backup'):
            c = OB.compare(w, S['owner_backup']); S['owner_compare'] = c
            check('O: the owner\'s LocalLow + Steam userdata files unchanged, no pending cloud upload', not c['diff'] and not c['remotecache_syncstate3'], c)
        S['checks'] = checks; json.dump(S, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    print(f'\n{len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
