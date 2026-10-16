#!/usr/bin/env python3
"""Windows (DSMP bottle, graphics null, audio off): a mod zip dropped while the game runs is installed by the loader in
game and turned on with Apply. Same real break as l1_pkg_test.py (Mac).
  w_pkg_test.py lock | unlock | install | run <out>
install: this worktree's build_win.sh kit (MODS = the Windows multiplayer module) -> install.bat in the bottle.
run: gcd-cdr not installed, game at the title (-batchmode -nographics), drop gcd-cdr-<v>.zip (the real package), loader
Refresh (test request), the new mod listed and off; Apply gcd-cdr (test request) -> the new game runs gcd-cdr."""
import json, os, shutil, subprocess, sys, time, glob
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_multiplayer/win'); import wloader as w
LW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = f'{H}/ds_multiplayer/rig/wine.lock'; MODS = f'{w.ROOT}/mods'; ACTIVE = f'{w.PROF}/data/Mods/active.json'
PKG = os.environ.get('PKG_ZIP', f'{H}/ds_launcher/loader/build/packages/gcd-cdr-2.0.0.zip')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []
def check(n, ok, d=''): checks.append({'name': n, 'ok': bool(ok), 'detail': str(d)[:400]}); log('PASS' if ok else 'FAIL', n, '' if ok else d)
def lock():
    busy = subprocess.run(['/bin/sh', '-c', "ps -axo etime=,comm= | grep -iE 'wine|\\.exe$' | grep -ve '-[0-9][0-9]:'"], capture_output=True, text=True).stdout.strip()
    if busy: sys.exit('wine busy: ' + busy[:300])
    os.mkdir(LOCK); open(f'{LOCK}/owner', 'w').write('loaderui\n'); log('wine lock taken')
def owned(): return os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().strip() == 'loaderui'
def unlock():
    if not owned(): return
    os.makedirs(f'{H}/ds_multiplayer/rig/old', exist_ok=True)
    os.replace(f'{LOCK}/owner', f'{H}/ds_multiplayer/rig/old/wine.lock.owner.loaderui.{int(time.time())}'); os.rmdir(LOCK); log('wine lock released')
def install():
    assert owned()
    env = dict(os.environ, MODS=f'{H}/ds_multiplayer/win/build/mods/multiplayer')
    r = subprocess.run(['nice', '-n', '19', 'zsh', f'{LW}/start/win/build_win.sh'], env=env, capture_output=True, text=True)
    log((r.stdout or r.stderr).strip().splitlines()[-1:])
    rc = w.install(f'{LW}/build/win/kit'); log('install rc', rc); return rc
def park(p):
    if os.path.exists(p):
        d = f'{w.ROOT}/retired'; os.makedirs(d, exist_ok=True); os.replace(p, f'{d}/test-{os.path.basename(p)}-{int(time.time())}')
def dlog():
    try: return open(f'{w.PROF}/dsload.log', errors='replace').read()
    except OSError: return ''
def logs_all():
    t = ''
    for f in ['dsload.prev.log', 'dsload.log'] + sorted(os.path.basename(x) for x in glob.glob(f'{w.PROF}/dsload.[0-9]*.log')):   # B: dsload.<pid>.log
        try: t += open(f'{w.PROF}/{f}', errors='replace').read()
        except OSError: pass
    return t
def request(obj):
    d = f'{w.PROF}/softrestart'; os.makedirs(d, exist_ok=True)
    open(f'{d}/reg_request.json.tmp', 'w').write(json.dumps(obj)); os.replace(f'{d}/reg_request.json.tmp', f'{d}/reg_request.json')
def wait_text(s, n0, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        if logs_all().count(s) > n0: return True
        time.sleep(1)
    return False
def start():
    assert owned() and w.bottle_silent()
    if not subprocess.run(['pgrep', '-f', 'dsmp_guard.py'], capture_output=True).stdout:
        subprocess.Popen(['python3', f'{H}/ds_multiplayer/win/tools/dsmp_guard.py', '7200'], start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DSRIG_PORT=str(w.PORT), DSMP_GUARD_TAG='1', DSL_NO_ALERT='1', DSL_SOFT_TEST='1', WINEDLLOVERRIDES='winhttp=n,b')
    for k in ('DSRIG_DIR', 'DSRIG_RANGES', 'DSL_MODS_DIR', 'DSL_PROFILE', 'DSL_LOG_DIR', 'DSML_SESSION', 'DSML_USER_MODS_DIR'): env.pop(k, None)
    subprocess.Popen(['nice', '-n', '19', f'{w.C}/wine', '--bottle', w.BOTTLE, w.WROOT + '\\Dungeon Settlers Modded.exe', '-batchmode', '-nographics'],
                     env=env, stdout=open(f'{LW}/build/win/w_pkg_wine.out', 'a'), stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
def title(limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            s = w.cmd({'cmd': 'status'}, timeout=5)
            if s.get('boot_ready') and s.get('scene') == 1: return s
        except Exception: pass
        time.sleep(5)
    return None
def run(out):
    os.makedirs(out, exist_ok=True)
    zname = os.path.basename(PKG); gid = zname.rsplit('-', 1)[0]
    park(f'{MODS}/{gid}')
    for f in glob.glob(f'{MODS}/*.zip') + glob.glob(f'{w.PROF}/dsload.[0-9]*.log'): park(f)
    os.makedirs(os.path.dirname(ACTIVE), exist_ok=True); json.dump({'enabled': ['multiplayer'], 'order': ['multiplayer']}, open(ACTIVE, 'w'))
    t0 = time.time(); start()
    try:
        s = title(600); check('title reached (multiplayer only)', s, s); log('title after', round(time.time() - t0), 's')
        check('gcd-cdr not installed at start', not os.path.exists(f'{MODS}/{gid}') and 'running gameplay set vanilla' in dlog(), dlog()[-300:])
        shutil.copy2(PKG, f'{MODS}/{zname}')                           # the player drops the zip while the game runs
        n0 = logs_all().count('reg test: pkg_refresh'); request({'cmd': 'pkg_refresh'})
        ok = wait_text('reg test: pkg_refresh', n0, 60)
        line = [l for l in dlog().splitlines() if 'reg test: pkg_refresh' in l][-1:] if ok else []
        check('Refresh installs the zip and lists the new mod (off)', line and ' 1 new' in line[0] and f'{gid}:off' in line[0], line)
        check('zip moved to mods\\.installed, folder mods\\' + gid, os.path.exists(f'{MODS}/.installed/{zname}') and os.path.exists(f'{MODS}/{gid}/mod.json'))
        request({'cmd': 'apply', 'ids': ['multiplayer', gid]})
        def blog():                                                    # the new game's own log: dsload.<pid>.log
            return ''.join(open(x, errors='replace').read() for x in glob.glob(f'{w.PROF}/dsload.[0-9]*.log'))
        t1 = time.time()
        while time.time() - t1 < 300 and 'running gameplay set ' + gid not in blog(): time.sleep(2)
        B = blog(); L = logs_all(); runs = [l for l in B.splitlines() if 'running gameplay set' in l]
        check('after Apply the new game runs ' + gid + ' (module opened and hooked)', runs and gid in runs[-1] and f'{gid} ' in B and f'{gid} SkillComponent' in B, runs[-2:] or [l for l in L.splitlines() if 'soft' in l][-6:])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        w.stop(); time.sleep(3)
        for f in ['starter.log', 'dsload.log', 'dsload.prev.log'] + [os.path.basename(x) for x in glob.glob(f'{w.PROF}/dsload.[0-9]*.log')]:
            if os.path.exists(f'{w.PROF}/{f}'): shutil.copy2(f'{w.PROF}/{f}', f'{out}/{f}')
        json.dump(checks, open(f'{out}/checks.json', 'w'), indent=1)
        log(f"{sum(c['ok'] for c in checks)}/{len(checks)} checks")
if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'lock': lock()
    elif a[0] == 'unlock': unlock()
    elif a[0] == 'install': sys.exit(install())
    elif a[0] == 'run':
        run(os.path.expanduser(a[1]))
        sys.exit(1 if not all(c['ok'] for c in checks) or not checks else 0)   # any FAIL or no check: exit 1
