#!/usr/bin/env python3
"""Mod data sync (v0.1.16, lane moddata) on a leased pair of hidden loader copies (never the owner's game, saves or screen).

Both copies run multiplayer + sync-demo (test/moddata/demo: mod.json "sync": ["campaign/*.json"]). Before the run:
  host  data/ModData/sync-demo/campaign/a.json "host-a", campaign/new.json "host-new", notes.txt "host-notes" (not sync)
  guest data/ModData/sync-demo/campaign/a.json "guest-a", campaign/own.json "guest-own", notes.txt "guest-notes"
The host loads a save and hosts; the guest joins by address (title > Multiplayer > Join by address > Connect).
Checks: the module gets its folder (API 6); the join point carries the block (host log); during the session the guest
has exactly the host's sync files (a.json, new.json; own.json put aside) and its own notes.txt; moddata.json says
"guest"; the two play 30 s with 0 desyncs. Then the host quits: the guest is back on the title and has its own files
again (a.json "guest-a", own.json back, new.json gone, the stash gone, moddata.json "none").

  moddata_pair.py            inputs: MD_BUILD (loader build with the moddata multiplayer pin, default ../../build),
                             MD_SRC_APP (a ready hidden copy to clone, default ~/ds_launcher/loader/test_env/instances/Z)
Copies MDH / MDG in ../../test_env/instances (own bundle ids, background-only, windows staged far off every display,
DSL_NOTCH=0, nice 5). A guard kills them if a window shows on a display or one becomes frontmost. Lease: ~/ds_multiplayer
rig (lane moddata). Machine cap 6 test copies, load <= 40. Output: ../../test_env/moddata/<time>/ (logs, results.json)."""
import json, os, shutil, signal, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import lrig  # noqa: E402
RIGDIR = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(1, RIGDIR)
import rig as realrig  # noqa: E402  (leases only)
rp = lrig.rigports                                              # each run's own TCP port + the EXTRA PLAYER guard (rigports.py)

lrig.INST = os.path.join(L, 'test_env', 'instances')
H, G = 'MDH', 'MDG'
MOD = 'sync-demo'
STAGE = '-20000,-20000'
SAVE = os.path.join(lrig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
B = dict(MULTIPLAYER=1, JOIN_ADDR=14, CONNECT=15)
BUILD = os.environ.get('MD_BUILD', os.path.join(L, 'build'))
OUT = os.path.join(L, 'test_env', 'moddata', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
checks, info = [], {}
stop_guard = threading.Event(); violations = []
HOST_FILES = {'campaign/a.json': 'host-a', 'campaign/new.json': 'host-new', 'notes.txt': 'host-notes'}
GUEST_FILES = {'campaign/a.json': 'guest-a', 'campaign/own.json': 'guest-own', 'notes.txt': 'guest-notes'}


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


def make_copy(x):
    app = os.path.join(lrig.idir(x), 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.environ.get('MD_SRC_APP', os.path.expanduser('~/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app'))
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


def demo_mod():
    d = os.path.join(OUT, 'mod', MOD); os.makedirs(d, exist_ok=True)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-mmacosx-version-min=12.0', '-I' + os.path.join(L, 'sdk'), '-dynamiclib',
                    '-install_name', f'@rpath/{MOD}.dylib', '-o', os.path.join(d, f'{MOD}.dylib'), os.path.join(HERE, 'demo', 'sync_demo.c')], check=True)
    shutil.copy2(os.path.join(HERE, 'demo', 'mod.json'), os.path.join(d, 'mod.json'))
    return d


def mdir(x):
    return os.path.join(lrig.idir(x), 'data', 'ModData', MOD)


def kit(x, mod, files):
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    park(k); park(os.path.join(I, 'data', 'ModData'))
    md = os.path.join(k, 'loader', 'mods'); os.makedirs(md); os.makedirs(os.path.join(k, 'mods')); os.makedirs(os.path.join(k, 'update'))
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(k, 'loader', 'dsload.dylib'))
    shutil.copy2(os.path.join(os.path.dirname(L), 'RELEASE'), os.path.join(k, 'loader', 'RELEASE'))
    shutil.copytree(os.path.join(BUILD, 'mods', 'multiplayer'), os.path.join(md, 'multiplayer'))
    shutil.copytree(mod, os.path.join(md, MOD))
    mods = os.path.join(I, 'data', 'Mods')
    for f in os.listdir(mods):
        if f.endswith('.json'):
            park(os.path.join(mods, f))
    json.dump({'enabled': ['multiplayer', MOD], 'order': ['multiplayer', MOD]}, open(os.path.join(mods, 'active.json'), 'w'))
    shutil.copy2(SAVE, os.path.join(I, 'data', 'Saves', os.path.basename(SAVE)))
    for rel, t in files.items():
        p = os.path.join(mdir(x), rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, 'w').write(t)
    for f in ('rig.log', 'dsload.log', 'fp.log', 'player.log', 'mp.json', 'clipboard.txt', 'softrestart'):
        park(os.path.join(I, f))


def start(x):
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    lrig.require_background_only(x); lrig.prepare_data(x); lrig.require_windowed(x)
    if os.path.exists(lrig.sockpath(x)):
        os.unlink(lrig.sockpath(x))                             # our copy's own socket file
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '30', 'DYLD_INSERT_LIBRARIES': os.path.join(k, 'loader', 'dsload.dylib'),
           'DSL_MODS_DIR': os.path.join(k, 'loader', 'mods'), 'DSML_ROOT': k, 'DSML_USER_MODS_DIR': os.path.join(k, 'mods'),
           'DSL_PROFILE': os.path.join(I, 'data', 'Mods', 'active.json'), 'DSML_SESSION': os.path.join(I, 'data', 'Mods', 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSL_UPDATE_API': 'http://127.0.0.1:9',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSMP_TEST': '1'}
    env.update(lrig.soft_test_env())
    env.update(lrig.ctl_env())                                  # test control channel (v0.1.16: off without DSRIG_CTL)
    os.makedirs(os.path.join(k, 'crash'), exist_ok=True)       # the crash-report question answered: it would sit on the title
    with open(os.path.join(k, 'crash', 'consent.json'), 'w') as f: f.write('{"consent":"no"}\n')
    sf = rp.loader_settings_file(I, env)
    rp.clear_stale_mp(I)                                        # an mp.json from before this run (another lane's): set aside
    rp.set_host_port(I, rp.port_for([I], 'moddata ' + x, share=True), sf)   # a menu host: this run's port, never 47777
    rp.check_start(I, sf)                                       # GUARD: mp.json / host_port at a port this run does not own = refused
    rp.note_started(I, x)
    args = [exe(x), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(lrig.pidfile(x), 'w').write(f'{p.pid}\n')
    log(x, 'started pid', p.pid)
    return p.pid


def stop(x):
    if pids(x):
        try:
            rp.end_check(lrig.send, x, lrig.idir(x))             # EXTRA PLAYER: more players in its session than this run's copies
        except Exception as e:
            log(x, 'stop check failed', repr(e))
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
    t0 = time.time(); said = 0
    while True:
        cnt, ld = lrig.running_test_copies(), os.getloadavg()[0]
        if cnt + n <= lrig.CAP_COPIES and ld <= lrig.CAP_LOAD and lrig.swap_gb() <= lrig.CAP_SWAP_GB:
            return
        if time.time() - t0 > 1800:
            raise SystemExit(f'refused: no room for {n} test copies for 30 min ({cnt} run, load {ld:.0f})')
        if time.time() - said > 60:
            log(f'waiting for room: {cnt} test copies run (+{n} needed, cap {lrig.CAP_COPIES}), load {ld:.0f}'); said = time.time()
        time.sleep(15)


def files(x):
    """{relative path: text} of the copy's sync-demo data folder (stash and dot files not included)"""
    out = {}
    for r, ds, fs in os.walk(mdir(x)):
        ds[:] = [d for d in ds if not d.startswith('.')]
        for f in fs:
            if not f.startswith('.'):
                p = os.path.join(r, f); out[os.path.relpath(p, mdir(x))] = open(p).read()
    return out


def session_state(x):
    try:
        return json.load(open(os.path.join(lrig.idir(x), 'data', 'ModData', 'moddata.json'))).get('session')
    except Exception:
        return None


def players(phase):
    """the session's player count (rigports): above 2 = EXTRA PLAYER, a FAIL (2026-10-25: runs of two lanes joined each other)"""
    ok, lines = rp.players_guard(lrig.send, [H, G], 2, phase)
    return check(f'players: no extra player at the {phase}', ok, '; '.join(lines))


def run(port):
    mod = demo_mod()
    kit(H, mod, HOST_FILES); kit(G, mod, GUEST_FILES)
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(lrig.idir(H), 'mp.json'), 'w'))
    room_for(2)
    start(H)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    dl = flog(H, 'dsload.log')
    check('the module gets its own data folder (API 6) and reads its file there',
          f'data folder {mdir(H)}, campaign/a.json "host-a"' in dl, [l for l in dl.splitlines() if 'sync-demo:' in l][-1:])
    check('moddata.json lists the folder and the sync pattern', (lambda j: j.get('session') == 'none' and j['mods'][MOD]['sync'] == ['campaign/*.json'])(
        json.load(open(os.path.join(lrig.idir(H), 'data', 'ModData', 'moddata.json')))), '')
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'moddata_base.json'); shutil.copy2(SAVE, src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H).get('det', {}).get('locked'), timeout=900, every=2)

    start(G)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3)
    check('guest before: its own files', files(G) == GUEST_FILES, files(G))
    hpos = os.path.getsize(os.path.join(lrig.idir(H), 'rig.log')) if os.path.exists(os.path.join(lrig.idir(H), 'rig.log')) else 0
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=B['CONNECT'])
    log('guest: join by address sent')
    wait('guest joined, loaded and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                      and not s.get('mp', {}).get('needs_state') else None)(st(G)), timeout=900, every=1)
    players('start')
    hl = [l for l in (flog(H, 'dsload.log') + flog(H, 'rig.log', hpos)).splitlines() if 'moddata' in l]
    gl = [l for l in (flog(G, 'dsload.log') + flog(G, 'rig.log')).splitlines() if 'moddata' in l]
    info['host_moddata_lines'] = hl[-6:]; info['guest_moddata_lines'] = gl[-6:]
    check('host packed the sync files into the join point (2 files, notes.txt not)', any(f'{MOD}: 2 sync file(s)' in l and 'packed' in l for l in hl), hl[-3:])
    gf = files(G); want = {'campaign/a.json': 'host-a', 'campaign/new.json': 'host-new', 'notes.txt': 'guest-notes'}
    check("during the session the guest has exactly the host's sync files and its own non-sync file", gf == want, gf)
    check('moddata.json says "guest" and the guest log has the session line', session_state(G) == 'guest' and any('guest session' in l for l in gl), gl[-2:])

    # ---- play 30 s: the join with the block is a normal session (orders, 0 desyncs)
    us = [u for u in c(H, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    m0 = {x: st(x).get('mp', {}).get('desyncs', 0) for x in (H, G)}
    t0 = time.time(); k = 0
    while time.time() - t0 < 30:
        realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == 'moddata'])
        if us:
            u = us[k % len(us)]; k += 1
            c(H, cmd='ord_hover', n=0)
            c(H, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': u['guid']})
            c(H, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f"{u['cell'][0] + 3},{u['cell'][1] + 2}", 'u8@24': 0})
        time.sleep(6)
    sh, sg = st(H), st(G)
    d = {x: s.get('mp', {}).get('desyncs', 0) - m0[x] for x, s in ((H, sh), (G, sg))}
    check('30 s of play after the join: world advanced, 0 desyncs', not any(d.values()) and (sg.get('tick') or 0) > 0, f'desyncs {d}, ticks {sh.get("tick")} {sg.get("tick")}')
    players('end')                                              # the last moment both are in the session (the host quits next)

    # ---- the host quits: the guest goes back to the title and gets its own files back
    stop(H)
    wait('guest back on the title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('det', {}).get('locked') else None)(st(G)), timeout=300, every=1)
    wait('guest restore', lambda: session_state(G) == 'none', timeout=60, every=1)
    gf = files(G)
    stash = os.path.join(lrig.idir(G), 'data', 'ModData', '.mp_stash')
    check("after the session the guest has its own files again (own.json back, host's new.json gone, no stash)",
          gf == GUEST_FILES and not os.path.exists(stash), f'{gf}; stash {os.path.exists(stash)}')
    check('host files untouched', files(H) == HOST_FILES, files(H))


def main():
    if os.path.exists(os.path.join(RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    for x in (H, G):
        if pids(x):
            raise SystemExit(f'copy {x} already running: {pids(x)}')
    t0 = time.time(); pair = realrig.lease('moddata', os.getpid())
    while not pair and time.time() - t0 < 1800:
        if int(time.time() - t0) % 300 < 15:
            log('waiting for a free pair lease')
        time.sleep(15); pair = realrig.lease('moddata', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease for 30 min')
    port = realrig.PAIR_PORTS[pair]
    log('lease', pair, 'port', port, 'build', BUILD, 'out', OUT)
    g = threading.Thread(target=guard, daemon=True); g.start()
    res = {}
    try:
        info['owner_prefs_before'] = lrig.check_prefs(restore=False)
        for x in (H, G):
            make_copy(x)
        run(port)
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
        realrig.release(pair)
        info['owner_prefs_after'] = lrig.check_prefs(restore=False)
        res = {'checks': checks, 'all_pass': all(k['ok'] for k in checks) and bool(checks), 'info': info}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
