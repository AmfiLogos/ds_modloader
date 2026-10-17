#!/usr/bin/env python3
"""The player's game saves in the modded game (owner 2026-10-16 / 10-17), on test copy O (hidden, muted, staged; take a
lease first: ~/ds_multiplayer/rig/rig.py lease savesync --pid <this pid>). Own save folders only: the "game" folder is
<copy>/gameroot/data/Saves, the modded folder <copy>/data/Saves. The owner's saves and prefs: hash / check before-after.
Real breaks this catches:
 A  modded copy (multiplayer + gcd-cdr): a modded save named like a game save (old first-start copy, other bytes) is
    set aside, never deleted; the Load list shows the game save and the modded-only save in two groups; the game save
    loads with no warning (gcd-cdr changes nothing in the save), is played and saved: the GAME file gets the progress,
    nothing is written to the modded folder.
 N  "normal game" copy (no loader, no mod: the test rig dsrig.dylib only; its save folder = the game folder): the same save loads
    with the progress of run A (TimeSaveData.Tick); the modded-only save is not there and does not load.
 B  modded copy with hell-difficulty (save_dependent): loading the game save asks first (no loading screen); Cancel = not
    loaded; again + Load = loaded, the answer kept in worlds.json "save_warned".
 C  same mods again: no question.
 D  delete (owner 2026-10-17): the save's remove answered Yes in the game's own box asks once more ("also used by
    Dungeon Settlers without mods"); Cancel = the game file stays; Delete = the game file is deleted in the game folder.
    (The world's delete button is off in build 25846389: CampaignSaveDeletionService.IsAvailable returns false.)
 The set-aside notice of run A names the newer file (the modded copy) and the copy's path.
Output: ~/ds_launcher/loader/test_env/l1_out/saves (or argv[1]): logs, shots, summary.json."""
import json, os, sys, time, glob, shutil, signal, subprocess, threading, hashlib, socket
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
r = T.r
X = os.environ.get('DSL_TEST_COPY', 'O')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/saves')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT
check, log = T.check, T.log
I = T.I
GROOT = os.path.join(I, 'gameroot'); GSAVES = os.path.join(GROOT, 'data', 'Saves'); MSAVES = os.path.join(I, 'data', 'Saves')
ELVEN = T.SAVE; GUID = T.GUID
MOON = 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0'
SEED = os.path.join(r.SEED_DATA, 'Saves')
LBUILD = os.path.join(os.path.dirname(HERE), 'build', 'dsload.dylib')                    # this worktree's loader
MODSRC = os.environ.get('DSL_TEST_MODS', os.path.expanduser('~/ds_launcher/loader/build/mods'))   # built mods (unchanged here)
DSRIG = os.environ.get('DSL_TEST_DSRIG', os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib'))


def tree_sum(d):
    h = hashlib.sha256()
    for root, _, files in sorted(os.walk(d)):
        for f in sorted(files):
            p = os.path.join(root, f); h.update(p.encode()); h.update(open(p, 'rb').read())
    return h.hexdigest()[:16]


def tick_of(path):
    return json.load(open(path))['TimeSaveData']['Tick']


def install():
    app = os.path.join(I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    shutil.copy2(LBUILD, os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    for m in ('multiplayer', 'gcd-cdr', 'hell-difficulty'):
        src = os.path.join(MODSRC, m)
        for f in glob.glob(os.path.join(src, '**', '*'), recursive=True):
            if os.path.isfile(f):
                t = os.path.join(md, m, os.path.relpath(f, src)); os.makedirs(os.path.dirname(t), exist_ok=True)
                shutil.copy2(f, t + '.tmp'); os.replace(t + '.tmp', t)
        src_json = os.path.join(os.path.dirname(HERE), 'modules', m, 'mod.json')   # this tree's mod.json ("save_dependent")
        if m != 'multiplayer' and os.path.exists(src_json): shutil.copy2(src_json, os.path.join(md, m, 'mod.json'))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    log('installed', sorted(os.listdir(md)))


def start_modded(enabled, worlds=None):
    """the modded game as the starters start it (DSML_GAME_SAVES = the game folder); worlds = worlds.json to write, or
    None = keep the one of the last run"""
    T.require_hidden_copy(os.path.join(I, 'DungeonSettlers.app'))
    r.prepare_data(X)
    os.makedirs(T.MODS, exist_ok=True)
    prof = {'enabled': list(enabled), 'order': list(enabled)}
    if 'hell-difficulty' in enabled: prof['provided'] = ['hell-difficulty-data']   # its table data: the starter applies it, not this test
    json.dump(prof, open(os.path.join(T.MODS, 'active.json'), 'w'))
    if worlds is not None: json.dump({'worlds': worlds}, open(os.path.join(T.MODS, 'worlds.json'), 'w'))
    if os.path.exists(r.sockpath(X)): os.unlink(r.sockpath(X))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(T.MODS, 'active.json'), 'DSML_SESSION': os.path.join(T.MODS, 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': GSAVES, 'DSL_CLOUD': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': T.STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0'}
    env.update(r.soft_test_env())
    r.require_windowed(X)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('modded copy started pid', p.pid, enabled)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title'); time.sleep(3)
    return p.pid


def gsend(obj, timeout=20):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(timeout)
    t0 = time.time()
    while True:
        try: s.connect(os.path.join(GROOT, 'ctl.sock')); break
        except (ConnectionRefusedError, FileNotFoundError):
            if time.time() - t0 > 120: raise
            time.sleep(0.5); s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(timeout)
    s.sendall((json.dumps(obj) + '\n').encode()); b = b''
    while not b.endswith(b'\n'):
        k = s.recv(1 << 20)
        if not k: break
        b += k
    s.close(); return json.loads(b.decode())


def start_normal():
    """the normal game: no loader, no mod; the multiplayer test rig dsrig.dylib (control socket, batchmode boot; it changes
    nothing in how a save is read: research 2026-10-17) + the save folder
    (persistentDataPath -> gameroot/data, so its Saves folder is the game folder of run A)"""
    for f in ('ctl.sock',):
        if os.path.exists(os.path.join(GROOT, f)): os.unlink(os.path.join(GROOT, f))
    shutil.copy2(os.path.join(I, 'data', 'UserSetting.json'), os.path.join(GROOT, 'data', 'UserSetting.json'))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin',
           'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': GROOT, 'DSRIG_BATCH': '1', 'DSRIG_FPS': '30',
           'DYLD_INSERT_LIBRARIES': DSRIG}
    args = [os.path.join(I, r.EXE_REL), '-batchmode', '-logFile', os.path.join(GROOT, 'player.log')]
    p = subprocess.Popen(args, env=env, cwd=GROOT, stdin=subprocess.DEVNULL, stdout=open(os.path.join(GROOT, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    log('normal copy started pid', p.pid)
    t0 = time.time(); st = {}
    while time.time() - t0 < 240:
        try:
            st = gsend({'cmd': 'status'})
            if st.get('scene') == 1 and st.get('boot_ready'): break
        except Exception: pass
        time.sleep(1)
    return p, st


def stop_all():
    for p in T.game_pids():
        try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        if T.alive(p): os.kill(p, signal.SIGKILL)
    time.sleep(1)


def wait_line(s, mark, limit=30):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in T.dlog()[mark:]: return True
        time.sleep(0.3)
    return False


def sreq(obj):
    """a test request read by dsl_saves.c (DSL_SOFT_TEST)"""
    tmp = os.path.join(T.SDIR, 'saves_request.json.tmp')
    open(tmp, 'w').write(json.dumps(obj)); os.replace(tmp, os.path.join(T.SDIR, 'saves_request.json'))


def load_and_wait(name, limit=180):
    r.send(X, {'cmd': 'load_save', 'name': name}, timeout=20, connect_wait=0)
    return T.wait(lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', 0) > 0, 'world ' + name, limit)


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    real0 = tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    S = {'violations': T.violations, 'checks': T.checks}
    # folders: the game folder has Elven (a game save), the modded folder Moonlight (modded only) + an old first-start
    # copy of Elven with other bytes (as if played modded once)
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (MSAVES, GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(T.MODS, 'save_copies'), os.path.join(I, 'mp_tags.txt')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), GSAVES)
    shutil.copy2(os.path.join(SEED, MOON + '.json'), MSAVES)
    stale = open(os.path.join(SEED, ELVEN + '.json'), 'rb').read().replace(b'"Tick": 8938508', b'"Tick": 8938000')
    if stale == open(os.path.join(SEED, ELVEN + '.json'), 'rb').read(): stale = stale + b'\n'
    open(os.path.join(MSAVES, ELVEN + '.json'), 'wb').write(stale)
    g_seed_tick = tick_of(os.path.join(GSAVES, ELVEN + '.json'))
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    try:
        # ---------------------------------------------------------------- A
        start_modded(('multiplayer', 'gcd-cdr'), worlds={})
        L = T.dlog()
        check('A: loader lists the game folder', 'saves: on: 1 game save(s)' in L, [l for l in L.splitlines() if 'saves:' in l][:4])
        copies = glob.glob(os.path.join(T.MODS, 'save_copies', ELVEN + '-modded-copy-*.json'))
        check('A: old modded copy set aside (same bytes), not deleted; gone from the modded folder',
              len(copies) == 1 and open(copies[0], 'rb').read() == stale and not os.path.exists(os.path.join(MSAVES, ELVEN + '.json')), copies)
        check('A: the player is told once (different bytes)', T.wait_log('saves: told the player about 1 set-aside', 20))
        L = T.dlog()
        check('A: the notice says the modded copy is the newer file and where it is',
              'the old modded copy is NEWER than your game save' in L and len(copies) == 1 and copies[0] in L,
              [l for l in L.splitlines() if 'NEWER' in l or 'save_copies' in l][-3:])
        T.shot('A0_title_set_aside_notice'); T.request({'cmd': 'warn_cancel'}); time.sleep(1.5)
        r.send(X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('A: Load list in two groups (1 game save, 1 modded save)', T.wait_log('groups: 1 game, 1 modded', 20),
              [l for l in T.dlog().splitlines() if 'rows: Load list rebuilt' in l][-2:])
        time.sleep(1.5); T.shot('A1_load_list_groups')
        mark = len(T.dlog())
        st = load_and_wait(ELVEN)
        check('A: game save loads with gcd-cdr, no question', 'saves: warning shown' not in T.dlog()[mark:], T.dlog()[mark:][-300:])
        t0 = st.get('tick'); S['A_tick_loaded'] = t0
        r.send(X, {'cmd': 'set_speed', 'speed': 3}, timeout=10, connect_wait=0); time.sleep(40)
        t1 = T.status().get('tick'); S['A_tick_played'] = t1
        check('A: the world runs (tick advanced)', t1 and t0 and t1 > t0, (t0, t1))
        mark = len(T.dlog()); before = os.stat(os.path.join(GSAVES, ELVEN + '.json')).st_mtime
        T.request({'cmd': 'save_file', 'name': ELVEN}); wait_line('reg test: save_file', mark, 30); time.sleep(3)
        gt = tick_of(os.path.join(GSAVES, ELVEN + '.json')); S['A_tick_saved'] = gt
        check('A: saved into the GAME folder with the progress', os.stat(os.path.join(GSAVES, ELVEN + '.json')).st_mtime > before and gt > g_seed_tick and gt >= t1,
              (g_seed_tick, gt, T.dlog()[mark:][-400:]))
        check('A: nothing of that world written to the modded folder', sorted(os.listdir(MSAVES)) == [MOON + '.json'], os.listdir(MSAVES))
        stop_all()
        # ---------------------------------------------------------------- N
        np_, st = start_normal()
        check('N: normal copy at the title, its save folder = the game folder', st.get('scene') == 1 and st.get('persistentDataPath', '').endswith('gameroot/data'), st)
        gsend({'cmd': 'load_save', 'name': MOON}); time.sleep(12)
        st = gsend({'cmd': 'status'})
        check('N: the modded-only save is not in the normal game (not loaded)', st.get('scene') == 1 and not os.path.exists(os.path.join(GSAVES, MOON + '.json')), st)
        gsend({'cmd': 'load_save', 'name': ELVEN}); t0 = time.time()
        while time.time() - t0 < 180:
            st = gsend({'cmd': 'status'})
            if st.get('scene') == 2 and not st.get('busy') and st.get('tick', 0) > 0: break
            time.sleep(0.5)
        S['N_tick_loaded'] = st.get('tick')
        check('N: the game save loads in the normal game with the progress made in the modded game',
              st.get('scene') == 2 and gt <= st.get('tick', 0) < gt + 3000, (gt, st.get('tick')))
        cache = json.load(open(os.path.join(GROOT, 'data', 'SaveHeaderCache.json'))) if os.path.exists(os.path.join(GROOT, 'data', 'SaveHeaderCache.json')) else {}
        names = json.dumps(cache)
        check("N: the normal game's own list has the game save, not the modded one", ELVEN in names and MOON not in names, names[:300])
        try: gsend({'cmd': 'quit'}, timeout=5)
        except Exception: pass
        time.sleep(3)
        if np_.poll() is None: np_.kill()
        # ---------------------------------------------------------------- B
        shutil.copy2(os.path.join(T.MODS, 'worlds.json'), os.path.join(OUT, 'worlds_after_A.json'))
        start_modded(('multiplayer', 'gcd-cdr', 'hell-difficulty'), worlds={})
        check('B: hell-difficulty runs', 'hell-difficulty' in json.dumps(json.load(open(os.path.join(I, 'dsload_modset.json')))), '')
        mark = len(T.dlog())
        r.send(X, {'cmd': 'load_save', 'name': ELVEN}, timeout=20, connect_wait=0)
        check('B: the question shows before loading', wait_line('saves: warning shown for ' + ELVEN, mark, 20), T.dlog()[mark:][-300:])
        time.sleep(2); T.shot('B1_warning'); st = T.status()
        check('B: no loading started (still the title)', st.get('scene') == 1, st)
        T.request({'cmd': 'warn_cancel'}); time.sleep(6); st = T.status()
        check('B: Cancel = not loaded', st.get('scene') == 1 and 'answered Cancel' in T.dlog()[mark:], st)
        mark = len(T.dlog())
        r.send(X, {'cmd': 'load_save', 'name': ELVEN}, timeout=20, connect_wait=0)
        check('B: asked again after Cancel', wait_line('saves: warning shown for ' + ELVEN, mark, 20))
        time.sleep(1.5); T.request({'cmd': 'warn_load'})
        st = T.wait(lambda s: s.get('scene') == 2, 'world after Load (hell without its table half may keep loading here: the scene is enough)', 180)
        wj = json.load(open(os.path.join(T.MODS, 'worlds.json')))
        check('B: Load = loaded; the answer kept for this world', st.get('scene') == 2 and wj.get('save_warned', {}).get(GUID) == 'hell-difficulty', wj.get('save_warned'))
        stop_all()
        # ---------------------------------------------------------------- C
        start_modded(('multiplayer', 'gcd-cdr', 'hell-difficulty'))
        mark = len(T.dlog())
        r.send(X, {'cmd': 'load_save', 'name': ELVEN}, timeout=20, connect_wait=0); st = T.wait(lambda s: s.get('scene') == 2, 'world (C)', 180)
        check('C: same world, same mods: no question again', st.get('scene') == 2 and 'saves: warning shown' not in T.dlog()[mark:], T.dlog()[mark:][-300:])
        stop_all()
        # ---------------------------------------------------------------- D
        start_modded(('multiplayer', 'gcd-cdr'))
        r.send(X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0); time.sleep(3)
        gfile = os.path.join(GSAVES, ELVEN + '.json')
        mark = len(T.dlog()); sreq({'cmd': 'del_save', 'name': ELVEN})
        check('D: deleting a game save asks first', wait_line('saves: delete question shown for save ' + ELVEN, mark, 20), T.dlog()[mark:][-400:])
        time.sleep(1.5); T.shot('D1_delete_question')
        T.request({'cmd': 'warn_cancel'}); wait_line('delete answered Cancel', mark, 20); time.sleep(4)
        check('D: Cancel = the game file is NOT deleted', os.path.exists(gfile), sorted(os.listdir(GSAVES)))
        mark = len(T.dlog()); sreq({'cmd': 'del_save', 'name': ELVEN})
        wait_line('saves: delete question shown', mark, 20); time.sleep(1.5); T.request({'cmd': 'warn_load'})
        t0 = time.time()
        while time.time() - t0 < 20 and os.path.exists(gfile): time.sleep(0.5)
        check('D: Delete = the game file is deleted in the game folder (the modded save stays)',
              not os.path.exists(gfile) and os.path.exists(os.path.join(MSAVES, MOON + '.json')),
              (sorted(os.listdir(GSAVES)), [l for l in T.dlog()[mark:].splitlines() if 'saves' in l][-5:]))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        stop_all()
        for p in subprocess.run(['pgrep', '-f', 'gameroot/player.log'], capture_output=True, text=True).stdout.split():
            os.kill(int(p), signal.SIGKILL)
        check("owner's real save folder unchanged (hash before/after)", tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
