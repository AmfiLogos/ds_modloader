#!/usr/bin/env python3
"""mac_update_test.py <work dir> <apps dir> <instance app> [tests]: auto-update on a hidden test copy (never a window on screen).

<apps dir>/<release>/Dungeon Settlers Modded.app = built apps (13.3 old, 13.4 new, 13.5 broken: its loader aborts).
The game runs as a hidden instance copy (LSBackgroundOnly, off-screen stage, the l1_test environment) with the loader of
a fake older install: ROOT/loader from the 13.3 app, DSML_APP = a copy of the 13.3 app in <work>/Applications.
A local release server (relserver.py) stands in for GitHub. Tests (default all): on, off, rollback, cold."""
import glob, json, os, shutil, signal, subprocess, sys, time

W, APPS, INST_APP = (os.path.abspath(a) for a in sys.argv[1:4])
TESTS = sys.argv[4].split(',') if len(sys.argv) > 4 else ['on', 'picker', 'off', 'rollback', 'cold']
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 7999
BACKUP = os.path.expanduser('~/ds_multiplayer/backup_prefs_2026-10-12')       # read only (test settings source)
SEED = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827')
NAME = 'Dungeon Settlers Modded.app'
results = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    results.append((name, ok)); log('PASS' if ok else 'FAIL', name, detail)


def sh(*a):
    subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def release_dir(srv, tag, src_app, flags=()):
    d = os.path.join(srv, 'AmfiLogos', 'ds_multiplayer', tag); os.makedirs(d, exist_ok=True)
    st = os.path.join(srv, '.stage', tag); os.makedirs(st, exist_ok=True)
    sh('/usr/bin/ditto', src_app, os.path.join(st, NAME))
    sh('/usr/bin/ditto', '-c', '-k', '--keepParent', os.path.join(st, NAME), os.path.join(d, 'Dungeon-Settlers-Modded-Mac.zip'))
    for f in flags:
        open(os.path.join(d, f), 'w').close()


class Run:
    def __init__(self, name, srv_tags, auto=True, seed=True):
        self.d = os.path.join(W, name); os.makedirs(self.d)
        self.root = os.path.join(self.d, 'root'); self.apps = os.path.join(self.d, 'Applications'); self.i = os.path.join(self.d, 'inst')
        self.app = os.path.join(self.apps, NAME)
        sh('/usr/bin/ditto', os.path.join(APPS, '13.3', NAME), self.app)
        sh('/usr/bin/ditto', os.path.join(self.app, 'Contents/Resources/dsml'), os.path.join(self.root, 'loader'))
        sh('/usr/bin/xattr', '-cr', self.root)
        os.makedirs(os.path.join(self.root, 'update'))
        if not auto:
            json.dump({'auto': False}, open(os.path.join(self.root, 'update', 'settings.json'), 'w'))
        for sub in ('home', 'data/Mods'):
            os.makedirs(os.path.join(self.i, sub))
        json.dump({'enabled': ['gcd-cdr'], 'order': ['gcd-cdr']}, open(os.path.join(self.i, 'data/Mods/active.json'), 'w'))
        json.dump({'worlds': {}}, open(os.path.join(self.i, 'data/Mods/worlds.json'), 'w'))
        open(os.path.join(self.i, 'steam_appid.txt'), 'w').write('2798330\n')
        if seed:                                             # language chosen, sound off, small window (as lrig.prepare_data)
            us = json.load(open(os.path.join(BACKUP, 'UserSetting.json')))
            for k in list(us['SoundSettingData']):
                if k.endswith('Percent'):
                    us['SoundSettingData'][k] = 0.0
                if k.endswith('Mute'):
                    us['SoundSettingData'][k] = True
            g = us['GraphicSettingData']; g['VSyncEnabled'] = False; g['SaveCurrentScreenType'] = 1
            for a, b in (('ResolutionWidth', 'ResolutionHeight'), ('WindowedResolutionWidth', 'WindowedResolutionHeight'),
                         ('FullScreenResolutionWidth', 'FullScreenResolutionHeight')):
                g[a], g[b] = 960, 540
            us['GeneralSettingData']['IsKeyboardEnvironmentNoticeAcknowledged'] = True
            # the game's own data folder under the test HOME (no multiplayer mod here to redirect it to inst/data)
            for dd in (os.path.join(self.i, 'data'), os.path.join(self.i, 'home/Library/Application Support/CanOpener/Dungeon Settlers')):
                os.makedirs(dd, exist_ok=True)
                json.dump(us, open(os.path.join(dd, 'UserSetting.json'), 'w'), indent=1)
                shutil.copy2(os.path.join(SEED, 'OutGame.json'), os.path.join(dd, 'OutGame.json'))
        self.srv = os.path.join(self.d, 'srv')
        for tag, src, flags in srv_tags:
            release_dir(self.srv, tag, os.path.join(APPS, src, NAME), flags)
        self.server = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'relserver.py'), self.srv, str(PORT)],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        self.pids = []

    def start(self):
        env = {'HOME': os.path.join(self.i, 'home'), 'CFFIXED_USER_HOME': os.path.join(self.i, 'home'), 'USER': os.environ.get('USER', ''),
               'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
               'SteamAppId': '2798330', 'SteamGameId': '2798330',
               'DSRIG_DIR': self.i, 'DSRIG_FPS': '60', 'DSL_LOG_DIR': self.i,
               'DYLD_INSERT_LIBRARIES': os.path.join(self.root, 'loader', 'dsload.dylib'), 'DSL_MODS_DIR': os.path.join(self.root, 'loader', 'mods'),
               'DSL_PROFILE': os.path.join(self.i, 'data/Mods/active.json'), 'DSML_SESSION': os.path.join(self.i, 'data/Mods/worlds.json'),
               'DSML_DATA': os.path.join(self.i, 'data'), 'DSML_ROOT': self.root, 'DSML_APP': self.app,
               'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
               'DSL_UPDATE_API': f'http://127.0.0.1:{PORT}', 'DSL_UPDATE_TEST': '1'}
        args = [os.path.join(INST_APP, 'Contents/MacOS/Dungeon Settlers'), '-logFile', os.path.join(self.i, 'player.log'),
                '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
        p = subprocess.Popen(args, env=env, cwd=self.i, stdin=subprocess.DEVNULL, stdout=open(os.path.join(self.i, 'stdout.log'), 'ab'),
                             stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
        self.pids.append(p.pid); log('A started pid', p.pid)

    def logtext(self):
        t = ''
        for f in ('dsload.prev.log', 'dsload.log'):
            p = os.path.join(self.i, f)
            if os.path.exists(p):
                t += open(p, errors='replace').read()
        return t

    def wait(self, s, limit=240):
        t0 = time.time()
        while time.time() - t0 < limit:
            if s in self.logtext():
                return True
            time.sleep(1)
        return False

    def release_of(self):
        return (open(os.path.join(self.root, 'loader/RELEASE')).read().strip(),
                open(os.path.join(self.app, 'Contents/Resources/dsml/RELEASE')).read().strip())

    def requests(self):
        p = os.path.join(self.srv, 'requests.log')
        return open(p).read() if os.path.exists(p) else ''

    def stop(self):
        out = subprocess.run(['/bin/ps', '-axo', 'pid=,command='], capture_output=True, text=True).stdout
        for l in out.splitlines():
            pid, cmd = l.strip().split(' ', 1)
            if INST_APP in cmd and os.path.join(self.i, 'player.log') in cmd:
                try:
                    os.kill(int(pid), signal.SIGKILL)
                except OSError:
                    pass
        self.server.terminate()
        time.sleep(1)


def grep(t, s):
    return [l for l in t.splitlines() if s in l][-1:] or ['(none)']


if 'on' in TESTS:
    r = Run('on', [('v13.4', '13.4', ())])
    try:
        r.start()
        asked = r.wait('update: prompt shown: v13.4 is ready. Install and restart now?', 240)
        time.sleep(3)
        check('on: prompt shown, nothing installed before the click', asked and 'installed (' not in r.logtext() and r.release_of() == ('13.3', '13.3'))
        open(os.path.join(r.root, 'update', 'test_later'), 'w').close()
        btn = r.wait('update: corner button "Update to v13.4" shown', 60)
        check('on: Later -> corner button, still 13.3', btn and r.release_of() == ('13.3', '13.3'))
        open(os.path.join(r.root, 'update', 'test_click'), 'w').close()
        got = r.wait('update: v13.4 confirmed at the title', 300)
        t = r.logtext()
        check('on: download sha256 ok', 'Dungeon-Settlers-Modded-Mac.zip downloaded, sha256 ok' in t, grep(t, 'sha256')[0])
        check('on: installed + restart', 'update: v13.4 installed' in t and 'A: APPLY' in t, grep(t, 'installed (')[0])
        check('on: B confirmed at the title', got, grep(t, 'confirmed')[0])
        check('on: release 13.4 (loader, app)', r.release_of() == ('13.4', '13.4'), str(r.release_of()))
        bk = glob.glob(os.path.join(r.root, 'update', 'backup-13.3-*', 'app', NAME, 'Contents/Resources/dsml/RELEASE'))
        check('on: old app kept in the backup folder', bool(bk) and open(bk[0]).read().strip() == '13.3', bk[0] if bk else 'none')
        check('on: no quarantine on the new app', 'com.apple.quarantine' not in subprocess.run(['/usr/bin/xattr', '-lr', r.app], capture_output=True, text=True).stdout)
    finally:
        r.stop()

if 'picker' in TESTS:
    r = Run('picker', [('v13.4', '13.4', ())], seed=False)                     # first start: the Language picker is up
    try:
        r.start()
        dl = r.wait('Dungeon-Settlers-Modded-Mac.zip downloaded, sha256 ok', 240)
        time.sleep(20)
        t = r.logtext()
        check('picker: downloaded, but no corner and no prompt over the Language picker',
              dl and 'update: title corner shown' not in t and 'update: prompt shown' not in t)
    finally:
        r.stop()

if 'off' in TESTS:
    r = Run('off', [('v13.4', '13.4', ())], auto=False)
    try:
        r.start()
        shown = r.wait('update: corner button "Update to v13.4" shown', 240)
        time.sleep(3)
        check('off: button shown', shown)
        check('off: nothing downloaded', '/dl/' not in r.requests(), r.requests().replace('\n', ' | ')[:200])
        open(os.path.join(r.root, 'update', 'test_click'), 'w').close()
        got = r.wait('update: v13.4 confirmed at the title', 300)
        check('off: click -> install + restart + confirm', got and r.release_of() == ('13.4', '13.4'), str(r.release_of()))
    finally:
        r.stop()

if 'rollback' in TESTS:
    r = Run('rollback', [('v13.5', '13.5', ())])
    try:
        r.start()
        r.wait('update: prompt shown', 240)
        open(os.path.join(r.root, 'update', 'test_install'), 'w').close()
        got = r.wait('update: rolled back', 300)
        time.sleep(2)
        t = r.logtext()
        check('rollback: B failed -> rolled back', got, grep(t, 'soft restart FAILED')[0])
        check('rollback: release 13.3 again (loader, app)', r.release_of() == ('13.3', '13.3'), str(r.release_of()))
        bad = open(os.path.join(r.root, 'update', 'bad.txt')).read() if os.path.exists(os.path.join(r.root, 'update', 'bad.txt')) else ''
        check('rollback: 13.5 not offered again', 'kit 13.5' in bad, bad.strip())
        check('rollback: A still runs (old loader)', subprocess.run(['/bin/kill', '-0', str(r.pids[0])]).returncode == 0)
    finally:
        r.stop()

if 'cold' in TESTS:
    # an update that never reached the title: the stub's cold start rolls it back (stub dry run, no game start)
    r = Run('cold', [])
    r.server.terminate()
    try:
        bk = os.path.join(r.root, 'update', 'backup-13.3-test')
        os.makedirs(os.path.join(bk, 'app'))
        shutil.move(r.app, os.path.join(bk, 'app', NAME)); sh('/usr/bin/ditto', os.path.join(APPS, '13.4', NAME), r.app)
        shutil.move(os.path.join(r.root, 'loader'), os.path.join(bk, 'loader'))
        sh('/usr/bin/ditto', os.path.join(APPS, '13.4', NAME, 'Contents/Resources/dsml'), os.path.join(r.root, 'loader'))
        open(os.path.join(r.root, 'update', 'pending.txt'), 'w').write(
            f'LABEL\tv13.4\nKEY\tkit 13.4\nFROM\t13.3\nMOVE\t{r.app}\t{os.path.join(bk, "app", NAME)}\nMOVE\t{os.path.join(r.root, "loader")}\t{os.path.join(bk, "loader")}\n')
        env = dict(os.environ, DSMODDED_DRY_RUN='1', DSMODDED_ROOT=r.root, DSMP_NO_DIALOG='1')
        subprocess.run([os.path.join(r.app, 'Contents/MacOS/DS Modded')], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        check('cold: stub rolled back app + loader', r.release_of() == ('13.3', '13.3'), str(r.release_of()))
        check('cold: pending moved, 13.4 marked bad', not os.path.exists(os.path.join(r.root, 'update', 'pending.txt'))
              and 'kit 13.4' in open(os.path.join(r.root, 'update', 'bad.txt')).read())
    finally:
        pass

np = sum(ok for _, ok in results)
print(f'RESULT {np}/{len(results)} pass')
