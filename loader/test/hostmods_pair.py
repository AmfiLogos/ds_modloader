#!/usr/bin/env python3
""""Get the host's mods" (v0.1.14) on a leased pair of hidden loader copies (never the owner's game, saves or screen).

Scenario: the host runs gameplay mods X = gcd-cdr and Y = hell-difficulty. The guest does NOT have X installed and has Y
installed but OFF. The guest joins by address (title > Multiplayer > Join by address > Connect), is refused (MODSET),
the box shows (DSMP_HOSTMODS_TEST=1 clicks its button after 2 s), the loader downloads X from a LOCAL release server
(relserver.py = official releases: tag <id>-v<ver>, asset <id>-<ver>.zip, sha256 digest), turns X and Y on, the quiet
soft restart runs and the new guest process joins the same host by itself (DSMP_REJOIN). Then both play for --play
seconds with orders from both sides.
Checks: refusal + box, X downloaded (only official release URLs in requests.log, Y not downloaded again), X installed
at the host's version, the guest's mod-set lines equal the host's, rejoin without any command from the test, time from
the click to the rejoin, 0 desyncs (mp desyncs, DESYNC / FULL-STATE / deep lines, det tick_errors, fp.log equal).

  hostmods_pair.py [--play 240]

Inputs (phase 2 builds them; nothing is published):
  HM_BUILD     loader build folder: dsload.dylib + mods/multiplayer (build.sh with MP_PIN / MP_REPO = the hostmods
               ds_multiplayer commit). Default: ../build next to this file.
  HM_PACKAGES  folder with gcd-cdr-<v>.zip + hell-difficulty-<v>.zip (modules/package_mods.sh). Missing: Mac-only
               zips are made from HM_BUILD/mods/<id> (same layout: one folder <id>/).
  HM_SRC_APP   a ready hidden test copy to clone (make_instance.sh output). Default ~/ds_launcher/.../instances/Z.
  HM_SHOTS     where the two screenshots go (default the run folder).
Copies: HMH (host) and HMG (guest) in ../test_env/instances (own bundle ids, background-only, windows staged far
off every display, DSL_NOTCH=0, muted, nice 5). A guard kills them if a window shows on a display or one becomes
frontmost. Lease: ~/ds_multiplayer/rig/rig.py (lane hostmods; refused while rig/release.lock exists); the host
listens on the leased pair's port. Machine cap: 6 test copies (this test needs room for 3: the soft restart briefly
runs two guest processes), load <= 40. Output: ../test_env/hostmods/<time>/ (logs, results.json)."""
import glob, hashlib, json, os, re, shutil, signal, socket, subprocess, sys, threading, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lrig  # noqa: E402
RIGDIR = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(1, RIGDIR)
import rig as realrig  # noqa: E402  (leases only; its copies are never touched)

lrig.INST = os.path.join(L, 'test_env', 'instances')          # our own copies, not the shared ones
H, G = 'HMH', 'HMG'
X_ID, Y_ID = 'gcd-cdr', 'hell-difficulty'
REPO = 'AmfiLogos/ds_modloader'                                 # MOD_REPOS (dsl_update.c) of both
OFFICIAL = {'multiplayer': 'AmfiLogos/ds_multiplayer', 'custom-modes': 'AmfiLogos/ds_custommodes',
            'gcd-cdr': REPO, 'hell-difficulty': REPO}
STAGE = '-20000,-20000'
SAVE = os.path.join(lrig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
B = dict(MULTIPLAYER=1, JOIN_ADDR=14, CONNECT=15)
PLAY = int(sys.argv[sys.argv.index('--play') + 1]) if '--play' in sys.argv else 240
BUILD = os.environ.get('HM_BUILD', os.path.join(L, 'build'))
OUT = os.path.join(L, 'test_env', 'hostmods', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
SHOTS = os.environ.get('HM_SHOTS', OUT); os.makedirs(SHOTS, exist_ok=True)
checks, info = [], {}
stop_guard = threading.Event(); violations = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]})
    log('PASS' if ok else 'FAIL', name, detail)
    return ok


def c(x, **k):
    return lrig.send(x, k, timeout=120)


def st(x):
    s = {}
    for _ in range(20):
        try:
            s = c(x, cmd='status')
        except Exception as e:
            s = {'error': repr(e)}
        if 'tick' in s or 'scene' in s:
            return s
        time.sleep(1)
    raise RuntimeError(f'{x}: status not answered: {s}')


def wait(what, pred, timeout=600, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
        except (OSError, ValueError, RuntimeError):
            r = None
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def park(p):
    """never delete: set aside into the copy's old/ folder"""
    if os.path.lexists(p):
        d = os.path.join(os.path.dirname(p), 'old'); os.makedirs(d, exist_ok=True)
        os.replace(p, os.path.join(d, f'{os.path.basename(p)}.{time.time():.0f}'))


def exe(x):
    return os.path.join(lrig.idir(x), lrig.EXE_REL)


def pids(x):
    out = subprocess.run(['pgrep', '-f', exe(x)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def flog(x, name, pos=0):
    try:
        with open(os.path.join(lrig.idir(x), name), 'rb') as f:
            f.seek(pos); return f.read().decode('utf8', 'replace')
    except OSError:
        return ''


def fsize(x, name):
    p = os.path.join(lrig.idir(x), name)
    return os.path.getsize(p) if os.path.exists(p) else 0


def mod_version(d):
    return json.load(open(os.path.join(d, 'mod.json'), encoding='utf-8'))['version']


# ------------------------------------------------------------------ owner screen guard (both copies, every process)
def guard():
    while not stop_guard.is_set():
        ps = pids(H) + pids(G)
        if ps:
            bad = [w for w in lrig.windows(ps) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = lrig.front_pid()
            if bad or fp in ps:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in ps:
                    try:
                        os.kill(p, signal.SIGKILL)
                    except OSError:
                        pass
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


# ------------------------------------------------------------------ fixture: packages, release server, copies, kits
def packages():
    """{id: zip path}: the release packages (HM_PACKAGES), else Mac-only zips of HM_BUILD/mods/<id> (one folder <id>/)"""
    pk = os.environ.get('HM_PACKAGES', os.path.join(BUILD, 'packages')); res = {}
    for i in (X_ID, Y_ID):
        z = sorted(glob.glob(os.path.join(pk, f'{i}-*.zip')))
        if z:
            res[i] = z[-1]; continue
        src = os.path.join(BUILD, 'mods', i); ver = mod_version(src)
        zp = os.path.join(OUT, 'pkg', f'{i}-{ver}.zip'); os.makedirs(os.path.dirname(zp), exist_ok=True)
        with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as zf:
            for r, _, fs in os.walk(src):
                for f in sorted(fs):
                    if not f.startswith('.'):
                        p = os.path.join(r, f); zf.write(p, os.path.join(i, os.path.relpath(p, src)))
        res[i] = zp
    info['packages'] = {i: os.path.basename(z) for i, z in res.items()}
    return res


def zip_version(z, i):
    with zipfile.ZipFile(z) as zf:
        return json.loads(zf.read(f'{i}/mod.json'))['version']


def relserver(pk):
    """official releases of X and Y on a local stand-in for the GitHub API (never the real one)"""
    rd = os.path.join(OUT, 'rel')
    for i, z in pk.items():
        v = zip_version(z, i); td = os.path.join(rd, REPO, f'{i}-v{v}'); os.makedirs(td, exist_ok=True)
        shutil.copy2(z, os.path.join(td, f'{i}-{v}.zip'))
    s = socket.socket(); s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]; s.close()
    p = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'update', 'relserver.py'), rd, str(port)],
                         stdout=open(os.path.join(OUT, 'relserver.out'), 'w'), stderr=subprocess.STDOUT)
    url = f'http://127.0.0.1:{port}'
    wait('release server up', lambda: socket.create_connection(('127.0.0.1', port), 1).close() or True, 15, 0.2)
    return p, url, rd


def make_copy(x):
    """an APFS clone of a ready hidden test copy (make_instance.sh output) with its own bundle id"""
    app = os.path.join(lrig.idir(x), 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.environ.get('HM_SRC_APP', os.path.expanduser('~/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app'))
        if not os.path.exists(os.path.join(src, lrig.EXE_REL.split('/', 1)[1])):
            raise SystemExit(f'no ready test copy to clone at {src} (make_instance.sh first)')
        os.makedirs(lrig.idir(x), exist_ok=True)
        subprocess.run(['cp', '-c', '-R', src, app], check=True)
        pl = os.path.join(app, 'Contents', 'Info.plist')
        for k, t, v in (('CFBundleIdentifier', '-string', f'com.CanOpener.Dungeon-Settlers.mp{x}'), ('CFBundleName', '-string', f'DS rig {x}'),
                        ('LSBackgroundOnly', '-bool', 'YES'), ('LSUIElement', '-bool', 'YES')):
            subprocess.run(['plutil', '-replace', k, t, v, pl], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
        log(x, 'copy made from', src)
    lrig.require_background_only(x)
    for d in ('home', 'data/Saves', 'data/Mods'):
        os.makedirs(os.path.join(lrig.idir(x), d), exist_ok=True)


def kit(x, pk, installed, enabled):
    """the player's kit layout: <copy>/kit/loader/{dsload.dylib, RELEASE, mods/<id>}, <copy>/kit/mods (player mods)"""
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    park(k)
    md = os.path.join(k, 'loader', 'mods'); os.makedirs(md); os.makedirs(os.path.join(k, 'mods')); os.makedirs(os.path.join(k, 'update'))
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(k, 'loader', 'dsload.dylib'))
    shutil.copy2(os.path.join(os.path.dirname(L), 'RELEASE'), os.path.join(k, 'loader', 'RELEASE'))
    shutil.copytree(os.path.join(BUILD, 'mods', 'multiplayer'), os.path.join(md, 'multiplayer'))
    for i in installed:
        with zipfile.ZipFile(pk[i]) as zf:
            zf.extractall(md)
    mods = os.path.join(I, 'data', 'Mods')
    for f in glob.glob(os.path.join(mods, '*.json')):
        park(f)
    json.dump({'enabled': enabled, 'order': enabled}, open(os.path.join(mods, 'active.json'), 'w'))
    shutil.copy2(SAVE, os.path.join(I, 'data', 'Saves', os.path.basename(SAVE)))
    for f in ('rig.log', 'dsload.log', 'fp.log', 'player.log', 'mp.json', 'clipboard.txt', 'softrestart'):
        park(os.path.join(I, f))
    return k


def start(x, api, extra=None):
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    lrig.require_background_only(x); lrig.prepare_data(x); lrig.require_windowed(x)
    if os.path.exists(lrig.sockpath(x)):
        os.unlink(lrig.sockpath(x))                             # our copy's own socket file
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '30', 'DYLD_INSERT_LIBRARIES': os.path.join(k, 'loader', 'dsload.dylib'),
           'DSL_MODS_DIR': os.path.join(k, 'loader', 'mods'), 'DSML_ROOT': k, 'DSML_USER_MODS_DIR': os.path.join(k, 'mods'),
           'DSL_PROFILE': os.path.join(I, 'data', 'Mods', 'active.json'), 'DSML_SESSION': os.path.join(I, 'data', 'Mods', 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSL_UPDATE_API': api,
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSMP_TEST': '1'}
    env.update(lrig.soft_test_env())
    env.update(extra or {})
    args = [exe(x), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(lrig.pidfile(x), 'w').write(f'{p.pid}\n')
    log(x, 'started pid', p.pid)
    return p.pid


def stop(x):
    for p in pids(x):
        try:
            lrig.send(x, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception:
            pass
        for _ in range(50):
            if p not in pids(x):
                break
            time.sleep(0.1)
        if p in pids(x):
            os.kill(p, signal.SIGKILL); log(x, 'killed pid', p)


def room_for(n):
    """wait (10 min at most) until n more test copies fit under the machine cap and the load is low"""
    t0 = time.time(); said = 0
    while True:
        cnt, ld = lrig.running_test_copies(), os.getloadavg()[0]
        if cnt + n <= lrig.CAP_COPIES and ld <= lrig.CAP_LOAD and lrig.swap_gb() <= lrig.CAP_SWAP_GB:
            return
        if time.time() - t0 > 600:
            raise SystemExit(f'refused: no room for {n} test copies for 10 min ({cnt} run, load {ld:.0f})')
        if time.time() - said > 60:
            log(f'waiting for room: {cnt} test copies run (+{n} needed, cap {lrig.CAP_COPIES}), load {ld:.0f}'); said = time.time()
        time.sleep(15)


def order_move(x, guid, cell):
    c(x, cmd='ord_hover', n=0)
    r1 = c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
    r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
    return r1.get('ok') and r2.get('ok')


def shot(x, name):
    """the whole frame the player sees (backbuffer: game + every UI layer) of the hidden copy"""
    p = os.path.join(SHOTS, name)
    try:
        r = c(x, cmd='mpm_shot', path=p, how='backbuffer')
    except Exception as e:
        r = {'error': repr(e)}
    ok = os.path.exists(p) and os.path.getsize(p) > 1000
    log(x, 'screenshot', p, 'ok' if ok else f'MISSING {r}')
    return p if ok else None


def session_lines(x):
    try:
        return json.load(open(os.path.join(lrig.idir(x), 'data', 'Mods', 'worlds.json'))).get('modset_lines')
    except Exception:
        return None


def fp_map(x, t0):
    out = {}
    for line in flog(x, 'fp.log').splitlines():
        p = line.split()
        if len(p) >= 5 and p[0].isdigit() and int(p[0]) > t0:
            out[int(p[0])] = p[1]
    return out


# ------------------------------------------------------------------ the run
def run(port, api, rel, pk):
    xv = zip_version(pk[X_ID], X_ID); yv = zip_version(pk[Y_ID], Y_ID)
    info.update(host_versions={X_ID: xv, Y_ID: yv}, port=port, release_api=api)
    kit(H, pk, [X_ID, Y_ID], ['multiplayer', X_ID, Y_ID])
    gk = kit(G, pk, [Y_ID], ['multiplayer'])                     # X not installed, Y installed but off
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800},
              open(os.path.join(lrig.idir(H), 'mp.json'), 'w'))   # the guest has none: it joins through the menu

    room_for(3)
    start(H, api)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'hostmods_base.json'); shutil.copy2(SAVE, src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H).get('det', {}).get('locked'), timeout=900, every=2)
    hl = session_lines(H)
    info['host_lines'] = hl
    check('host runs X and Y', hl and any(l.startswith(f'{X_ID}@{xv}') for l in hl) and any(l.startswith(f'{Y_ID}@{yv}') for l in hl), hl)

    start(G, api, {'DSMP_HOSTMODS_TEST': '1'})
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3)
    gl0 = session_lines(G)
    check('guest before: X not installed, Y installed and off', not os.path.exists(os.path.join(gk, 'loader', 'mods', X_ID))
          and os.path.exists(os.path.join(gk, 'loader', 'mods', Y_ID)) and not gl0, f'guest lines {gl0}')
    a_pid = pids(G)[0]
    rpos = fsize(G, 'rig.log')
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=B['CONNECT'])
    t_join = time.time(); log('guest: join by address sent')

    box = wait('hostmods box', lambda: next((l for l in flog(G, 'rig.log', rpos).splitlines() if 'mp: hostmods box:' in l), None), timeout=180, every=0.1)
    t_box = time.time()
    time.sleep(1.0)                                              # the box has finished opening; the test click comes at 2 s
    info['box_shot'] = shot(G, 'hostmods_box_before_click.png')
    info['box_line'] = box
    gtxt = flog(G, 'rig.log', rpos)
    refusal = [l for l in gtxt.splitlines() if 'modset refusal' in l or 'REJ_MODSET' in l]
    check('guest refused (MODSET), the box shows the host mods', bool(refusal) and (X_ID in box or 'Global cooldown' in box), f'refusal {refusal[-1:]}; box {box}')

    # the test hook clicks 2 s after the box shows; from here on the test sends NOTHING to the guest until it rejoined.
    # Click time = the loader's "hostmods: get:" line (dsl_hostmods_get started), else box + 2 s.
    try:
        info['get_line'] = wait('hostmods get', lambda: next((l for l in flog(G, 'dsload.log').splitlines() if 'hostmods: get:' in l), None), timeout=20, every=0.1)
        t_click = time.time()
    except RuntimeError:
        info['get_line'] = None; t_click = t_box + 2.0
    b_pid = wait('new guest process (soft restart)', lambda: next((p for p in pids(G) if p != a_pid), None), timeout=600, every=0.2)
    t_b = time.time()
    open(lrig.pidfile(G), 'w').write(f'{b_pid}\n')
    wait('old guest process gone', lambda: a_pid not in pids(G), timeout=60, every=0.2)
    sg = wait('guest rejoined by itself, loaded and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                                       and not s.get('mp', {}).get('needs_state') else None)(st(G)), timeout=900, every=1)
    t_rejoin = time.time()
    info['timing_s'] = {'join_to_box': round(t_box - t_join, 1), 'click_to_new_process': round(t_b - t_click, 1), 'click_to_rejoined': round(t_rejoin - t_click, 1)}
    log('timing', info['timing_s'])
    dl = flog(G, 'dsload.log')
    info['guest_dsload_hostmods'] = [l for l in (flog(G, 'dsload.prev.log') + dl).splitlines() if 'hostmods' in l or 'update: mod' in l][-20:]
    check('guest rejoined the same host by itself (one process swap, no command from the test)', sg and b_pid in pids(G),
          f"tick {sg.get('tick')} map {sg.get('map')} mp {json.dumps({k: sg.get('mp', {}).get(k) for k in ('started', 'loads', 'present')})}")

    xd = os.path.join(gk, 'loader', 'mods', X_ID)
    gv = mod_version(xd) if os.path.exists(os.path.join(xd, 'mod.json')) else None
    check(f'X ({X_ID}) installed in the guest kit at the host version', gv == xv, f'guest {gv}, host {xv}, folder {xd}')
    act = json.load(open(os.path.join(lrig.idir(G), 'data', 'Mods', 'active.json'))).get('enabled', [])
    gl = session_lines(G)
    info['guest_lines'] = gl
    check('guest runs X and Y now: active set and mod-set lines equal to the host', X_ID in act and Y_ID in act and gl == hl, f'enabled {act}; guest lines {gl}')

    req = open(os.path.join(rel, 'requests.log')).read().splitlines() if os.path.exists(os.path.join(rel, 'requests.log')) else []
    info['requests'] = req
    ok_re = re.compile(r'^GET (/repos/(%s)/releases(\?[^ ]*)?|/dl/(%s)/([a-z0-9-]+)-v([0-9.]+)/\5-\6\.zip) \d{3}$'
                       % ('|'.join(map(re.escape, set(OFFICIAL.values()))), '|'.join(map(re.escape, set(OFFICIAL.values())))))
    bad = [l for l in req if not ok_re.match(l) or ('/dl/' in l and OFFICIAL.get(ok_re.match(l).group(5)) != ok_re.match(l).group(4))]
    xdl = [l for l in req if f'/dl/{REPO}/{X_ID}-v{xv}/{X_ID}-{xv}.zip 200' in l]
    ydl = [l for l in req if f'/dl/{REPO}/{Y_ID}-' in l]
    check('downloads: only official release URLs, X fetched once, Y not fetched again', not bad and len(xdl) == 1 and not ydl,
          f'{len(req)} requests, bad {bad[:3]}, X downloads {xdl}, Y downloads {ydl}')

    # ---- play: both give orders, the host keeps its unclaimed units busy
    time.sleep(5)
    info['rejoined_shot'] = shot(G, 'hostmods_rejoined_game.png')
    us = [u for u in c(H, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    mine_g = [u['guid'] for u in us[:2]]
    for g in mine_g:
        c(G, cmd='mp_claim', guid=g)
    time.sleep(3)
    for x in (H, G):
        c(x, cmd='det_frame_reset')
    rp = {x: fsize(x, 'rig.log') for x in (H, G)}
    m0 = {x: st(x).get('mp', {}) for x in (H, G)}
    e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (H, G)}
    tick0 = max(st(H)['tick'], st(G)['tick'])
    orders = {H: 0, G: 0}; t0 = time.time(); k = 0
    import random
    rnd = random.Random(14)
    while time.time() - t0 < PLAY:
        realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == 'hostmods'])
        for x in (H, G):
            try:
                uu = [u for u in c(x, cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
            except Exception as e:
                log('list_units', x, repr(e)[:120]); uu = []
            cand = [u for u in uu if (u['guid'] in mine_g) == (x == G)]
            if cand:
                u = rnd.choice(cand)
                if order_move(x, u['guid'], (u['cell'][0] + rnd.randint(-6, 6), u['cell'][1] + rnd.randint(-6, 6))):
                    orders[x] += 1
        k += 1
        if k == max(1, PLAY // 30):
            c(H, cmd='mp_speed', speed=2); c(G, cmd='mp_speed', speed=2); time.sleep(15)
            c(H, cmd='mp_speed', speed=1); c(G, cmd='mp_speed', speed=1)
        time.sleep(12)
    time.sleep(3)
    sh, sg = st(H), st(G)
    mh, mg = sh.get('mp', {}), sg.get('mp', {})
    lines = {kk: [l for l in flog(G, 'rig.log', rp[G]).splitlines() + flog(H, 'rig.log', rp[H]).splitlines() if pat in l]
             for kk, pat in (('desync', 'DESYNC:'), ('full_diff', 'FULL-STATE CHECK DIFFERS'), ('deep_diff', 'deep hash differs'), ('report', 'Desync report saved'))}
    a, b = fp_map(H, tick0), fp_map(G, tick0); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    te = {x: s.get('det', {}).get('tick_errors', 0) - e0[x] for x, s in ((H, sh), (G, sg))}
    stats = {'play_s': PLAY, 'orders': orders, 'ticks': [tick0, sh.get('tick'), sg.get('tick')], 'fp_common_ticks': len(com), 'fp_first_diff': first,
             'desyncs': {H: mh.get('desyncs', 0) - m0[H].get('desyncs', 0), G: mg.get('desyncs', 0) - m0[G].get('desyncs', 0)},
             'partials': mg.get('partials', 0) - m0[G].get('partials', 0), 'full_ok': mg.get('full_ok'), 'full_fails': mg.get('full_fails'),
             'tick_errors': te, **{kk: v[:5] for kk, v in lines.items()}}
    info['play'] = stats
    check('play: both gave orders, the world advanced', orders[H] >= 3 and orders[G] >= 3 and (sg.get('tick') or 0) > tick0 + 100, f"orders {orders}, ticks {stats['ticks']}")
    check('play: 0 desyncs (mp counters, desync / full-state / deep lines, det tick_errors, fp.log equal)',
          not any(stats['desyncs'].values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None
          and not stats['full_fails'], json.dumps({kk: stats[kk] for kk in ('desyncs', 'tick_errors', 'fp_common_ticks', 'fp_first_diff', 'full_ok', 'full_fails')}))


def main():
    if os.path.exists(os.path.join(RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    for x in (H, G):
        if pids(x):
            raise SystemExit(f'copy {x} already running: {pids(x)}')
    pair = realrig.lease('hostmods', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease')
    port = realrig.PAIR_PORTS[pair]
    log('lease', pair, 'port', port, 'build', BUILD, 'out', OUT)
    srv = None
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
        info['owner_prefs_before'] = lrig.check_prefs(restore=False)
        for x in (H, G):
            make_copy(x)
        pk = packages()
        srv, api, rel = relserver(pk)
        run(port, api, rel, pk)
    except BaseException as e:
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not violations, violations[:3])
        stop_guard.set()
        for x in (G, H):
            try:
                stop(x)
            except Exception as e:
                log('stop', x, repr(e))
        for x in (G, H):
            for f in ('dsload.log', 'dsload.prev.log', 'rig.log', 'player.log', 'stdout.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        if srv:
            srv.terminate()
        realrig.release(pair)
        info['owner_prefs_after'] = lrig.check_prefs(restore=False)
        res = {'checks': checks, 'all_pass': all(k['ok'] for k in checks) and bool(checks), 'info': info}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', os.path.join(OUT, 'results.json'))
    sys.exit(0 if res['all_pass'] else 1)


if __name__ == '__main__':
    main()
