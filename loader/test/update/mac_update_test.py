#!/usr/bin/env python3
"""mac_update_test.py <work dir> <apps dir> <instance app> [tests]: auto-update through the app's stub, on hidden test copies.

<apps dir>/<release>/Dungeon Settlers Modded.app = built apps: 0.1.3 (installed), 0.1.4 (new), 0.1.5 (broken: its loader
aborts at start). Each test has its own ROOT (DSMODDED_ROOT), app copy in <work>/<test>/Applications, test HOME; the stub
runs for real (install + execv) and starts <instance app> (hidden, LSBackgroundOnly; pinned in copy.json), or runs dry
(DSMODDED_DRY_RUN=1: the update steps, no game). The update window is made hidden (DSL_UPDATE_WINDOW=hidden). A local
release server (relserver.py) stands in for GitHub.
Tests (default all): on, off, failed, crash, mismatch, dmg, xvol."""
import glob, hashlib, json, os, re, shutil, signal, subprocess, sys, time

W, APPS, INST_APP = (os.path.abspath(a) for a in sys.argv[1:4])
TESTS = sys.argv[4].split(',') if len(sys.argv) > 4 else ['on', 'off', 'failed', 'crash', 'mismatch', 'dmg', 'xvol']
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, '../../src/dsl_update.c')).read()
KIT_REPO = re.search(r'#define KIT_REPO "([^"]+)"', SRC).group(1)
PORT = 7999
BACKUP = os.path.expanduser('~/ds_multiplayer/backup_prefs_2026-10-12')       # read only (test settings source)
SEED = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827')
NAME = 'Dungeon Settlers Modded.app'
OLD, NEW, BROKEN = '0.1.3', '0.1.4', '0.1.5'
# real-release mode (gate after a publish): DSL_REAL_RELEASE=0.1.7 -> the installed test copy is DSL_REAL_OLD (0.1.6.99)
# and the REAL GitHub release is offered: no local server, no DSL_UPDATE_API. Only the test copy is installed over.
REAL = os.environ.get('DSL_REAL_RELEASE', '')
if REAL:
    OLD, NEW = os.environ.get('DSL_REAL_OLD', '0.1.6.99'), REAL
    if len(sys.argv) <= 4:
        TESTS = ['on', 'off']
results = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    results.append((name, ok)); log('PASS' if ok else 'FAIL', name, detail)


def sh(*a):
    subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def zip_app(src_app, dst_zip):
    st = os.path.join(W, '.stage', str(time.time())); os.makedirs(st)          # outside the release folder
    sh('/usr/bin/ditto', src_app, os.path.join(st, NAME))
    sh('/usr/bin/ditto', '-c', '-k', '--keepParent', os.path.join(st, NAME), dst_zip)


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


class Run:
    def __init__(self, name, srv_tags=(), auto=True, apps_dir=None):
        self.d = os.path.join(W, name); os.makedirs(self.d)
        self.root = os.path.join(self.d, 'root'); self.home = os.path.join(self.d, 'home')
        self.apps = apps_dir or os.path.join(self.d, 'Applications'); self.app = os.path.join(self.apps, NAME)
        self.prof = os.path.join(self.root, 'profiles/modded'); self.u = os.path.join(self.root, 'update')
        sh('/usr/bin/ditto', os.path.join(APPS, OLD, NAME), self.app)
        os.makedirs(self.u); os.makedirs(os.path.join(self.home, 'Library/Logs'))
        if not auto:
            json.dump({'auto': False}, open(os.path.join(self.u, 'settings.json'), 'w'))
        data = os.path.join(self.prof, 'data')
        os.makedirs(os.path.join(data, 'Mods'))
        json.dump({'enabled': ['gcd-cdr'], 'order': ['gcd-cdr']}, open(os.path.join(data, 'Mods/active.json'), 'w'))
        json.dump({'worlds': {}}, open(os.path.join(data, 'Mods/worlds.json'), 'w'))
        open(os.path.join(data, '.copied_from_normal_game'), 'w').write('test\n')
        json.dump({'build': '25846389', 'app': INST_APP}, open(os.path.join(self.prof, 'copy.json'), 'w'))
        us = json.load(open(os.path.join(BACKUP, 'UserSetting.json')))      # language chosen, sound off, small window
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
        for dd in (data, os.path.join(self.prof, 'home/Library/Application Support/CanOpener/Dungeon Settlers')):
            os.makedirs(dd, exist_ok=True)
            json.dump(us, open(os.path.join(dd, 'UserSetting.json'), 'w'), indent=1)
            shutil.copy2(os.path.join(SEED, 'OutGame.json'), os.path.join(dd, 'OutGame.json'))
        self.srv = os.path.join(self.d, 'srv'); self.server = None
        for tag, src in ([] if REAL else srv_tags):
            d = os.path.join(self.srv, KIT_REPO, tag); os.makedirs(d)
            zip_app(os.path.join(APPS, src, NAME), os.path.join(d, 'Dungeon-Settlers-Modded-Mac.zip'))
        if srv_tags and not REAL:
            self.server = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'relserver.py'), self.srv, str(PORT)],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(0.5)

    def env(self, dry=False):
        e = {'HOME': self.home, 'USER': os.environ.get('USER', ''), 'LOGNAME': os.environ.get('LOGNAME', ''),
             'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
             'DSMODDED_ROOT': self.root, 'DSMP_NO_DIALOG': '1', 'DSMODDED_NO_STEAM': '1', 'DSL_UPDATE_WINDOW': 'hidden',
             'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
             'DSL_UPDATE_TEST': '1'}
        if not REAL:
            e['DSL_UPDATE_API'] = f'http://127.0.0.1:{PORT}'
        if dry:
            e['DSMODDED_DRY_RUN'] = '1'
        return e

    def stub(self, dry=False):
        """one start through the app's stub; dry: wait for it, else it becomes the game (left running)"""
        self.mark = len(self.logtext())
        args = [os.path.join(self.app, 'Contents/MacOS/DS Modded'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
        if dry:
            return subprocess.run(args, env=self.env(True), capture_output=True, text=True, timeout=300).stdout
        p = subprocess.Popen(args, env=self.env(), stdin=subprocess.DEVNULL, stdout=open(os.path.join(self.d, 'stdout.log'), 'ab'),
                             stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
        log('stub started pid', p.pid); self.proc = p

    def stublog(self):
        p = os.path.join(self.home, 'Library/Logs/DungeonSettlersModded.log')
        return open(p, errors='replace').read() if os.path.exists(p) else ''

    def logtext(self):
        t = ''
        for f in ('dsload.prev.log', 'dsload.log'):
            p = os.path.join(self.prof, f)
            if os.path.exists(p):
                t += open(p, errors='replace').read()
        return t

    def wait(self, s, limit=240, new=True):
        t0 = time.time()
        while time.time() - t0 < limit:
            if s in (self.logtext()[self.mark:] if new else self.logtext()):
                return True
            if self.proc.poll() is not None and s not in self.logtext()[self.mark:]:
                time.sleep(1)
                return s in self.logtext()[self.mark:]
            time.sleep(1)
        return False

    def release_of(self):
        rl = os.path.join(self.root, 'loader/RELEASE')
        return (open(rl).read().strip() if os.path.exists(rl) else '-',
                open(os.path.join(self.app, 'Contents/Resources/dsml/RELEASE')).read().strip())

    def pending(self):
        p = os.path.join(self.u, 'pending.txt')
        return open(p).read() if os.path.exists(p) else ''

    def ufile(self, f):
        p = os.path.join(self.u, f)
        return open(p).read() if os.path.exists(p) else ''

    def seed_ready(self, tag_ver, src, dmg=False):
        """a verified download as the loader leaves it (the download itself is tested in 'on')"""
        z = os.path.join(self.u, 'dl', 'Dungeon-Settlers-Modded-Mac.' + ('dmg' if dmg else 'zip')); os.makedirs(os.path.dirname(z), exist_ok=True)
        if os.path.exists(z):
            os.rename(z, z + f'.old{time.time()}')
        if dmg:                                                # as the release's .dmg: the app + an Applications link
            st = os.path.join(W, '.stage', str(time.time())); os.makedirs(st)
            sh('/usr/bin/ditto', os.path.join(APPS, src, NAME), os.path.join(st, NAME)); os.symlink('/Applications', os.path.join(st, 'Applications'))
            sh('/usr/bin/hdiutil', 'create', '-srcfolder', st, '-volname', 'DSMTEST', '-format', 'UDZO', z)
        else:
            zip_app(os.path.join(APPS, src, NAME), z)
        open(os.path.join(self.u, 'ready.txt'), 'w').write(
            f'KIND\tkit\nLABEL\tv{tag_ver}\nKEY\tkit {tag_ver}\nKIT\t{tag_ver}\t{z}\t{sha(z)}\n')

    def kill(self):
        out = subprocess.run(['/bin/ps', '-axo', 'pid=,command='], capture_output=True, text=True).stdout
        for l in out.splitlines():
            pid, cmd = l.strip().split(' ', 1)
            if INST_APP in cmd and '-screen-width 960' in cmd:
                try:
                    os.kill(int(pid), signal.SIGKILL)
                except OSError:
                    pass
        time.sleep(1)

    def stop(self):
        self.kill()
        if self.server:
            self.server.terminate()


def last(t, s):
    return [l for l in t.splitlines() if s in l][-1:] or ['(none)']


if 'on' in TESTS:
    r = Run('on', [('v' + NEW, NEW)])
    try:
        r.stub()                                               # 1st start: nothing downloaded, the game starts at once
        got = r.wait('update: ready.txt written (' + NEW, 240)
        line = r.wait(f'update: corner line: {NEW} installs the next time you start the game.', 60)
        t = r.logtext()
        check('on: downloaded in the background (sha256 ok), ready.txt', got and 'downloaded, sha256 ok' in t, last(t, 'sha256')[0])
        check('on: corner says it installs next start; nothing installed yet', line and r.release_of() == (OLD, OLD), str(r.release_of()))
        r.kill()
        r.stub()                                               # 2nd start: the stub installs before the game
        conf = r.wait(f'update: v{NEW} confirmed at the title', 240)
        st = r.stublog()
        check('on: window shown while installing', f'update window up (hidden): Updating Dungeon Settlers Modded to {NEW}' in st,
              last(st, 'update window')[0])
        check('on: installed by the stub, new stub started', f'update: v{NEW} installed' in st and 'starting the new app' in st)
        check('on: game started, update confirmed at the title', conf and not r.pending(), last(r.logtext(), 'confirmed')[0])
        check('on: release 0.1.4 (loader, app)', r.release_of() == (NEW, NEW), str(r.release_of()))
        check('on: "Updated to" line', r.wait(f'update: corner line: Updated to {NEW}', 30, new=False))
        check('on: no quarantine on the new app', 'com.apple.quarantine' not in subprocess.run(['/usr/bin/xattr', '-lr', r.app], capture_output=True, text=True).stdout)
    finally:
        r.stop()

if 'off' in TESTS:
    r = Run('off', [('v' + NEW, NEW)], auto=False)
    try:
        r.stub()
        shown = r.wait(f'update: corner button "Update to {NEW}" shown', 240)
        time.sleep(3)
        reqs = open(os.path.join(r.srv, 'requests.log')).read() if os.path.exists(os.path.join(r.srv, 'requests.log')) else ''
        check('off: button shown, nothing downloaded', shown and not [l for l in reqs.splitlines() if '/dl/' in l and '/release-manifest.json' not in l], reqs.replace('\n', ' | ')[:200])   # the signed manifest (v0.1.16) may be read; no kit file
        open(os.path.join(r.u, 'test_click'), 'w').close()
        got = r.wait(f'update: corner line: {NEW} installs the next time you start the game.', 120)
        check('off: click -> downloaded, marked, one line; nothing installed now', got and 'CONSENT\t1' in r.ufile('ready.txt')
              and r.release_of() == (OLD, OLD), str(r.release_of()))
        r.kill()
        r.stub()
        conf = r.wait(f'update: v{NEW} confirmed at the title', 240)
        check('off: installed at the next start', conf and r.release_of() == (NEW, NEW), str(r.release_of()))
    finally:
        r.stop()

if 'failed' in TESTS:
    # the install fails after the app was swapped (ROOT/loader cannot move): everything back, old version starts, one line
    r = Run('failed')
    try:
        r.stub(dry=True)                                       # first start: ROOT/loader from the app
        r.seed_ready(NEW, NEW)
        sh('/usr/bin/chflags', 'uchg', os.path.join(r.root, 'loader'))
        r.stub()
        notice = r.wait('update: starter notice shown: The update to 0.1.4 could not be installed', 240)
        sh('/usr/bin/chflags', 'nouchg', os.path.join(r.root, 'loader'))
        st = r.stublog()
        check('failed: install failed, rolled back with no failed move', 'install of v0.1.4 failed' in st and ' 0 failed' in last(st, 'rolled back')[0],
              last(st, 'rolled back')[0])
        check('failed: old version back (loader, app) and running', r.release_of() == (OLD, OLD) and r.wait('update: title corner shown', 60, new=False), str(r.release_of()))
        check('failed: one line at the title, ready.txt moved away, no pending', notice and not r.ufile('ready.txt') and not r.pending())
    finally:
        r.stop()
        if os.path.exists(os.path.join(r.root, 'loader')):
            subprocess.run(['/usr/bin/chflags', 'nouchg', os.path.join(r.root, 'loader')])

if 'crash' in TESTS:
    # the new version crashes at its first start: one retry, then rolled back, not offered again
    r = Run('crash')
    try:
        r.stub(dry=True)
        r.seed_ready(BROKEN, BROKEN)
        r.stub(); r.proc.wait(120); time.sleep(2)              # install + first start (crashes)
        p1 = r.pending()
        check('crash: installed, first start crashed', r.release_of() == (BROKEN, BROKEN) and p1.count('STARTED') == 1 and 'DONE' in p1, str(r.release_of()))
        r.stub(); r.proc.wait(120); time.sleep(2)              # the retry (crashes again)
        p2 = r.pending()
        check('crash: one retry, not rolled back yet', r.release_of() == (BROKEN, BROKEN) and p2.count('STARTED') == 2, p2.count('STARTED'))
        r.stub()                                               # third start: rolled back, the old version runs
        notice = r.wait(f'update: starter notice shown: The update to {BROKEN} did not start. You have {OLD} again.', 240)
        check('crash: rolled back, old version runs, one line', notice and r.release_of() == (OLD, OLD) and not r.pending(), str(r.release_of()))
        check('crash: 0.1.5 not offered again', f'kit {BROKEN}' in r.ufile('bad.txt'), r.ufile('bad.txt').strip())
    finally:
        r.stop()

if 'mismatch' in TESTS:
    # tag v0.1.6 whose app says RELEASE 0.1.4: refused (else it is offered again at every start); twice = not offered again
    r = Run('mismatch')
    try:
        r.stub(dry=True)
        r.seed_ready('0.1.6', NEW)
        r.stub(dry=True)
        st = r.stublog()
        check('mismatch: refused, nothing changed', "the download's version number is wrong" in st and r.release_of() == (OLD, OLD)
              and not r.pending(), str(r.release_of()))
        check('mismatch: one line for the player', 'could not be installed' in r.ufile('notice.txt'), r.ufile('notice.txt').strip())
        r.seed_ready('0.1.6', NEW)
        r.stub(dry=True)
        check('mismatch: second failure -> not offered again', 'kit 0.1.6' in r.ufile('bad.txt'), r.ufile('bad.txt').strip())
    finally:
        r.stop()

if 'dmg' in TESTS:
    # the Mac release ships a .dmg: the stub installs from it (hdiutil attach, read only)
    r = Run('dmg')
    try:
        r.stub(dry=True)
        r.seed_ready(NEW, NEW, dmg=True)
        r.stub(dry=True)
        check('dmg: installed from the .dmg', r.release_of() == (NEW, NEW) and 'DONE' in r.pending(), str(r.release_of()) + ' ' + last(r.stublog(), 'update:')[0])
    finally:
        r.stop()

if 'xvol' in TESTS:
    # the app on another volume (a disk image): install and rollback move it across volumes
    img = os.path.join(W, 'xvol.dmg'); mnt = os.path.join(W, 'xvol_mnt')
    sh('/usr/bin/hdiutil', 'create', '-size', '200m', '-fs', 'APFS', '-volname', 'DSUPDX', img)
    os.makedirs(mnt); sh('/usr/bin/hdiutil', 'attach', '-nobrowse', '-mountpoint', mnt, img)
    try:
        r = Run('xvol', apps_dir=os.path.join(mnt, 'Applications'))
        r.stub(dry=True)
        r.seed_ready(NEW, NEW)
        r.stub(dry=True)                                       # install (app moves to the backup on the other volume) + 1st start
        check('xvol: installed across volumes', r.release_of() == (NEW, NEW) and r.pending().count('STARTED') == 1, str(r.release_of()))
        r.stub(dry=True); r.stub(dry=True)                     # never reached the title twice -> rollback across volumes
        check('xvol: rolled back across volumes', r.release_of() == (OLD, OLD) and not r.pending() and 'kit 0.1.4' in r.ufile('bad.txt'),
              str(r.release_of()) + ' ' + last(r.stublog(), 'rolled back')[0])
    finally:
        subprocess.run(['/usr/bin/hdiutil', 'detach', '-quiet', mnt])

np = sum(ok for _, ok in results)
print(f'RESULT {np}/{len(results)} pass')
