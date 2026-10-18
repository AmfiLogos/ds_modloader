#!/usr/bin/env python3
"""The game-save backup before a save_dependent mod with mod.json "save_backup": true plays a game save (ZET-U, owner
2026-10-18), on test copy W (DSL_TEST_COPY; hidden quiet copy through lrig.start: guard, cap, DSL_NOTCH=0; a hidden
window, not -batchmode, because the warning box is screenshotted). Never the owner's saves: the "game" folder is
<copy>/gameroot/data/Saves, seeded from the 2026-10-11 save backup; DSL_CLOUD=0.
Test mod: a copy of this tree's built gcd-cdr in <out>/testmod with mod.json + "save_dependent" + "save_backup" (the real
modules are not changed). The game folder holds the Elven world twice (autosave _0 and _1, same guid) + Moonlight
(another world).
Real breaks this catches:
 F  the copy fails (save_backups is a file): the world is NOT loaded, the player is told, the question comes back
 1  the warning names the backup folder (the player knows where the copy is)
 2  Cancel copies nothing
 3  Load copies the loaded save + the other save of the same world (not the other world), byte for byte, file times
    kept, into <DSML_DATA>/Mods/save_backups/<YYYY-MM-DD_HHMM>_<world>/ (the folder the warning named), BEFORE the
    world loads; then the world loads
 4  a second load of that world (new start, same mods): no question, no new copy
Output: ~/ds_launcher/loader/test_env/l1_out/savebackup (or argv[1]): logs, shots, summary.json; the warning's shot
also to $SHOT when set."""
import json, os, re, sys, time, glob, shutil, signal, subprocess, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
r = T.r
X = os.environ.get('DSL_TEST_COPY', 'W')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/savebackup')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT
check, log = T.check, T.log
I = T.I
GROOT = os.path.join(I, 'gameroot'); GSAVES = os.path.join(GROOT, 'data', 'Saves'); MSAVES = os.path.join(I, 'data', 'Saves')
BACKUPS = os.path.join(T.MODS, 'save_backups')
ELVEN = T.SAVE; ELVEN1 = ELVEN[:-1] + '1'
MOON = 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0'
SEED = os.path.join(r.SEED_DATA, 'Saves')
LB = os.path.dirname(HERE)
LBUILD = os.path.join(LB, 'build', 'dsload.dylib')                                   # this tree's loader
MODSRC = os.environ.get('DSL_TEST_MODS', os.path.join(LB, 'build', 'mods'))          # this tree's built mods
ENABLED = ['multiplayer', 'gcd-cdr']


LANGTYPE = os.environ.get('LANGTYPE')                    # the game's LanguageType for a look at a long language (4 ru, 3 de)
if LANGTYPE:
    _prep = r.prepare_data
    def _prep_lang(x):
        _prep(x); us = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = int(LANGTYPE); json.dump(s, open(us, 'w'), indent=1)
    r.prepare_data = _prep_lang


def sha(p): return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def aside(p):
    """never delete: an old file / folder goes to <copy>/old/<time>/"""
    if os.path.lexists(p):
        d = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))


def install():
    app = os.path.join(I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    shutil.copy2(LBUILD, os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    aside(os.path.join(fw, 'lang')); shutil.copytree(os.path.join(LB, 'lang'), os.path.join(fw, 'lang'))   # the loader's texts, as shipped
    tm = os.path.join(OUT, 'testmod', 'gcd-cdr')                                    # the test mod: built gcd-cdr + the two keys
    if os.path.exists(tm): shutil.move(tm, tm + '.%d' % time.time())
    shutil.copytree(os.path.join(MODSRC, 'gcd-cdr'), tm)
    mj = json.load(open(os.path.join(tm, 'mod.json'))); mj['save_dependent'] = True; mj['save_backup'] = True
    mj['name'] = 'ZET-U'; mj.pop('i18n', None)                                     # a short name like the real mod's
    json.dump(mj, open(os.path.join(tm, 'mod.json'), 'w'), indent=1)
    for m, src in (('multiplayer', os.path.join(MODSRC, 'multiplayer')), ('gcd-cdr', tm)):
        aside(os.path.join(md, m)); shutil.copytree(src, os.path.join(md, m))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    log('installed', sorted(os.listdir(md)))


def start(worlds=None):
    os.makedirs(T.MODS, exist_ok=True)
    json.dump({'enabled': ENABLED, 'order': ENABLED, 'provided': ['notch-fix']}, open(os.path.join(T.MODS, 'active.json'), 'w'))
    if worlds is not None: json.dump({'worlds': worlds}, open(os.path.join(T.MODS, 'worlds.json'), 'w'))
    os.environ.update({'DSL_PROFILE': os.path.join(T.MODS, 'active.json'), 'DSML_SESSION': os.path.join(T.MODS, 'worlds.json'),
                       'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': GSAVES, 'DSL_CLOUD': '0',
                       'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': T.STAGE, 'DSL_NO_ALERT': '1'})
    os.environ.update(r.soft_test_env())
    pid = r.start(X, window=True)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title'); time.sleep(3)
    return pid


def stop():
    if r.alive(r.read_pid(X)): r.stop(X)
    time.sleep(1)


def wait_line(s, mark, limit=30):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in T.dlog()[mark:]: return True
        time.sleep(0.3)
    return False


def load(name):
    r.send(X, {'cmd': 'load_save', 'name': name}, timeout=20, connect_wait=0)


def shot(name):
    """a fresh shot (an old file of that name moved aside first: T.shot stops waiting once the file exists)"""
    p = os.path.join(OUT, name + '.png')
    if os.path.exists(p): shutil.move(p, p + '.%d.old' % time.time())
    T.shot(name)
    return p if os.path.exists(p) else ''


def backups():
    return sorted(os.listdir(BACKUPS)) if os.path.isdir(BACKUPS) else []


def main():
    if r.alive(r.read_pid(X)) or T.game_pids(): raise SystemExit(f'copy {X} already running')
    real0 = sha(os.path.join(SEED, ELVEN + '.json'))
    S = {'checks': T.checks}
    gpath = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gpath) if os.path.exists(gpath) else 0   # this run's part only
    for p in (MSAVES, GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), BACKUPS, os.path.join(T.MODS, 'save_copies'), os.path.join(T.MODS, 'worlds.json')):
        aside(p)
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), GSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), os.path.join(GSAVES, ELVEN1 + '.json'))   # an older autosave of the same world
    os.utime(os.path.join(GSAVES, ELVEN1 + '.json'), (time.time() - 7200, time.time() - 7200))
    shutil.copy2(os.path.join(SEED, MOON + '.json'), GSAVES)
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    try:
        start(worlds={})
        # ------------------------------------------------------------ F: the copy fails -> not loaded, told, asked again
        open(BACKUPS, 'w').write('blocks the folder (test F)\n')
        mark = len(T.dlog()); load(ELVEN)
        check('F: the question shows', wait_line('saves: warning shown for ' + ELVEN, mark, 30), T.dlog()[mark:][-300:])
        time.sleep(1.5); T.request({'cmd': 'warn_load'})
        check('F: the backup fails', wait_line('saves: backup of ' + ELVEN + ' FAILED', mark, 20), T.dlog()[mark:][-400:])
        check('F: the player is told why', wait_line('This world was not loaded: its save files could not be copied first.' if not LANGTYPE else 'Not a directory', mark, 10))
        time.sleep(5); st = T.status()
        wj = json.load(open(os.path.join(T.MODS, 'worlds.json'))) if os.path.exists(os.path.join(T.MODS, 'worlds.json')) else {}
        check('F: NOT loaded (still the title), the answer not kept', st.get('scene') == 1 and T.GUID not in (wj.get('save_warned') or {}), (st.get('scene'), wj))
        shot('F_copy_failed_notice')
        T.request({'cmd': 'warn_cancel'}); time.sleep(1.5)          # closes the notice
        aside(BACKUPS)
        # ------------------------------------------------------------ 1 + 2: the folder named; Cancel copies nothing
        mark = len(T.dlog()); load(ELVEN)
        ok = wait_line('saves: warning shown for ' + ELVEN, mark, 30)
        L = T.dlog()[mark:]
        m = re.search(r'saves: warning shown for \S+ \(world [^)]*\), backup to (.+)', L)
        named = m.group(1).strip() if m else ''
        S['named_folder'] = named
        check('1: asked again after the failed copy', ok, L[-300:])
        check('1: the warning says where the copy goes (save_backups/<date>_<world>)',
              named.startswith(BACKUPS + os.sep) and re.fullmatch(r'\d{4}-\d{2}-\d{2}_\d{4}_Elven_Expedition', os.path.basename(named))
              and (('Load first keeps a copy of its save files in:\n' if not LANGTYPE else '') + named) in L, named)
        time.sleep(2); sp = shot('1_warning_with_folder')
        if os.environ.get('SHOT') and sp:
            os.makedirs(os.path.dirname(os.environ['SHOT']), exist_ok=True); shutil.copy2(sp, os.environ['SHOT'])
        T.request({'cmd': 'warn_cancel'}); wait_line('answered Cancel', mark, 10); time.sleep(4)
        check('2: Cancel = not loaded, nothing copied', T.status().get('scene') == 1 and backups() == [], backups())
        # ------------------------------------------------------------ 3: Load = copies first, then the world
        before = {f: (sha(os.path.join(GSAVES, f)), os.stat(os.path.join(GSAVES, f)).st_mtime) for f in os.listdir(GSAVES)}
        mark = len(T.dlog()); load(ELVEN)
        wait_line('saves: warning shown for ' + ELVEN, mark, 30); time.sleep(1.5)
        L = T.dlog()[mark:]; m = re.search(r'backup to (.+)', L); named = m.group(1).strip() if m else named
        T.request({'cmd': 'warn_load'})
        check('3: the backup is logged with its file count', wait_line('saves: backup of %s (world %s): 2 file(s) copied to %s' % (ELVEN, T.GUID, named), mark, 30),
              [l for l in T.dlog()[mark:].splitlines() if 'backup' in l][-3:])
        L = T.dlog()[mark:]
        check('3: copied before the world loads', 0 <= L.find('saves: backup of') < L.find('saves: warning answered Load'), '')
        bd = os.path.join(BACKUPS, backups()[0]) if len(backups()) == 1 else ''
        got = sorted(os.listdir(bd)) if bd else []
        S['backup_folder'] = bd; S['backup_files'] = got
        check('3: one folder, the one the warning named', bd == named, (backups(), named))
        check('3: the loaded save + the other save of that world, not the other world', got == sorted([ELVEN + '.json', ELVEN1 + '.json']), got)
        check('3: byte-identical copies of the files as they were before Load, file times kept',
              got and all(sha(os.path.join(bd, f)) == before[f][0] and abs(os.stat(os.path.join(bd, f)).st_mtime - before[f][1]) < 1 for f in got), '')
        check('3: the game folder still has every file (copies, not a move)', sorted(os.listdir(GSAVES)) == sorted(before), os.listdir(GSAVES))
        st = T.wait(lambda s: s.get('scene') == 2, 'world after Load', 180)
        wj = json.load(open(os.path.join(T.MODS, 'worlds.json')))
        check('3: then the world loads; the answer kept', st.get('scene') == 2 and wj.get('save_warned', {}).get(T.GUID) == 'gcd-cdr', wj.get('save_warned'))
        stop()
        # ------------------------------------------------------------ 4: same world, same mods: no question, no copy
        start()
        mark = len(T.dlog()); load(ELVEN)
        st = T.wait(lambda s: s.get('scene') == 2, 'world (4)', 180)
        L = T.dlog()[mark:]
        check('4: second load: no question, no new copy', st.get('scene') == 2 and 'saves: warning shown' not in L and 'saves: backup of' not in L and len(backups()) == 1,
              (backups(), L[-300:]))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        stop()
        check("seed save unchanged", sha(os.path.join(SEED, ELVEN + '.json')) == real0)
        for f in ['dsload.log', 'dsload.log.prev', 'guard.log']:
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
        g = open(gpath, errors='replace').read()[g0:] if os.path.exists(gpath) else ''
        check('guard: never a visible window, never frontmost', 'GUARD KILLED' not in g, g[-300:])
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or not T.checks else 0)
