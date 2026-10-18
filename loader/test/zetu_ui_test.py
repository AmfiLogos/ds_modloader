#!/usr/bin/env python3
"""ZET-U "max units in the dungeon" = 8: the expedition screens and the dungeon with 8 units, on hidden test copy Q
(lrig.start: guard, cap, DSL_NOTCH=0; a hidden window, not -batchmode, so the UI draws for the shots). One rig lease.
Never the owner's game, saves or prefs: the seed save (2026-10-11 backup, 7 units) is loaded from a copy in <out>.

  zetu_ui_test.py run [out]          the whole proof (below); exit 1 on a failed check
  zetu_ui_test.py up [out]           lease + start + load the save, then hold until `down` (manual look)
  zetu_ui_test.py cmd '<json>'       rig socket command to the running copy
  zetu_ui_test.py probe '<line>'     zetu-probe request (recruit | select_all | toggle N | result)
  zetu_ui_test.py down

Real breaks the run catches (owner rule: 8 units work without UI overlap or crashes):
 1  the game's party cap follows the setting (8 units selected in the expedition window, not 4)
 2  the supplies step has a panel per selected unit (8), inside the 4 stock panels' row, none overlapping
 3  the bottom portraits of the region step: 8 shown, none overlapping; the count text says "/8"
 4  the 8 units arrive in the dungeon
 5  Inventory / Status / Skill windows with all 8 selected: no exception in player.log
 6  the result screen lists the 8 units (portraits shown, none overlapping)
Shots: <out>/*.png, also copied to $ZETU_SHOTS when set."""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = 'Q'; I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
SEED = os.path.join(r.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
STOPF = os.path.join(I, 'zetu_ui_stop')
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''
def plog():
    try: return open(os.path.join(I, 'player.log'), errors='replace').read()
    except OSError: return ''
def cmd(o, timeout=60): return r.send(X, o, timeout=timeout, connect_wait=5)
def status(): return cmd({'cmd': 'status'}, 10)
def request(o):                                   # loader test request (DSL_SOFT_TEST=1)
    d = os.path.join(I, 'softrestart'); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'reg_request.json.tmp'), 'w').write(json.dumps(o)); os.replace(os.path.join(d, 'reg_request.json.tmp'), os.path.join(d, 'reg_request.json'))
def wait_line(s, mark=0, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit:
        if s in dlog()[mark:]: return True
        time.sleep(0.3)
    return False
def probe(line, limit=20):
    mark = len(dlog())
    p = os.path.join(I, 'zetu_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l for l in dlog()[mark:].splitlines() if 'zetu-probe: ' in l]
        if L: return L
        time.sleep(0.3)
    return []
def teams(**k): k['cmd'] = 'ui_teams'; return cmd(k)
def shot(out, name):
    p = os.path.join(out, name + '.png')
    if os.path.exists(p): shutil.move(p, p + '.%d.old' % time.time())
    rc = teams(action='shot', path=p)
    time.sleep(1.5)
    ok = os.path.exists(p)
    log('shot', name, 'ok' if ok else ('MISSING %s' % rc))
    if ok and os.environ.get('ZETU_SHOTS'):
        os.makedirs(os.environ['ZETU_SHOTS'], exist_ok=True); shutil.copy2(p, os.path.join(os.environ['ZETU_SHOTS'], name + '.png'))
    return p if ok else ''
def units(m):
    u = cmd({'cmd': 'list_units', 'map': m}).get('units') or []
    return [x for x in u if x.get('faction') == 1 and not x.get('dead')]


def aside(p, out):
    """never delete: a file / folder of the copy goes to <out>/aside/"""
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))


def lease():
    lane = 'zetu-ui-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def disk_free_gb():
    s = os.statvfs(H); return s.f_bavail * s.f_frsize / 1e9


def up(out):
    """mods of this tree + the probe; profile with zetu-suite max_units 8; copy Q started hidden; the seed save loaded"""
    if r.alive(r.read_pid(X)): raise SystemExit('copy Q already running')
    if disk_free_gb() < 30: raise SystemExit('less than 30 GB free disk')
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    shutil.copytree(os.path.join(LB, 'build', 'mods'), md)
    pd = os.path.join(md, 'zetu-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/zetu-probe.dylib',
                    '-o', os.path.join(pd, 'zetu-probe.dylib'), os.path.join(HERE, 'zetu_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'zetu-probe.dylib')], capture_output=True)
    json.dump({'id': 'zetu-probe', 'name': 'zetu probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'zetu-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
    en = ['multiplayer', 'zetu-suite', 'zetu-probe']
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': en, 'order': en, 'settings': {'zetu-suite': {'max_units': 8}}}, open(prof, 'w'))
    for p in ('mp.json', 'dsload.log', 'player.log', 'zetu_probe_req.txt', os.path.join('data', 'Saves'), os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    gp = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gp) if os.path.exists(gp) else 0
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1',
                       'DSL_SOFT_STAGE': '-20000,-20000'})
    os.environ.pop('DSML_SESSION', None)
    os.environ.update(r.soft_test_env())
    r.start(X, window=True)
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    L = dlog()
    check('setting read: max units 8', 'max units in the dungeon 8' in L, [l for l in L.splitlines() if 'zetu' in l][-5:])
    check('zetu UI ready', 'zetu-suite' in L and 'expedition UI' in L, [l for l in L.splitlines() if 'zetu-suite' in l][-6:])
    check('probe ready', 'zetu-probe: ready' in L)
    src = os.path.join(out, 'save_in', os.path.basename(SEED)); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(SEED, src)
    mark = len(dlog())
    log('load', cmd({'cmd': 'load_save', 'file': src}))
    t0 = time.time()
    while time.time() - t0 < 400:                     # the save-backup question of zetu-suite (save_backup): Load
        if 'saves: warning shown' in dlog()[mark:]: time.sleep(1.5); request({'cmd': 'warn_load'}); break
        try:
            if status().get('scene') == 2: break
        except Exception: pass
        time.sleep(1)
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
    return g0


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def wait_map(m, limit=120):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            s = status()
            if s.get('map') == m and not s.get('busy'): return s
        except Exception: pass
        time.sleep(1)
    return status()


def run(out):
    S = {}
    g0 = up(out)
    cmd({'cmd': 'set_speed', 'speed': 1})
    st = status(); log('status', {k: st.get(k) for k in ('scene', 'map', 'tick')})
    # ---- the seed save: 4 units in the dungeon, 3 at the base. Back to the base through the game's result screen.
    if units(1):
        log('probe', probe('result')); time.sleep(4)
        shot(out, 'results_4_stock')
        d = units(1)
        log('back to base', cmd({'cmd': 'trn_changemap', 'from': 1, 'to': 0, 'guids': ','.join(u['guid'] for u in d)}))
        wait_map(0, 120); time.sleep(3)
    base = units(0)
    log('base units', len(base))
    for _ in range(4):                                 # recruit until 8 (the game's own recruit event)
        if len(base) >= 8: break
        log('probe', probe('recruit')); time.sleep(4)
        base = units(0); log('base units', len(base))
    check('8 player units at the base', len(base) >= 8, len(base))
    g8 = [u['guid'] for u in base[:8]]
    # ---- expedition window, region step: 8 selected
    log('go', teams(action='go')); time.sleep(3)
    log('select', teams(action='stock_select', units=','.join(g8))); time.sleep(2)
    L = dlog()
    m = re.findall(r'zetu-suite.*expedition: (\d+) selected', L)
    shot(out, 'expedition_8_select')
    S['select_line'] = [l for l in L.splitlines() if 'zetu-suite' in l and ('portraits:' in l or 'count text' in l)][-3:]
    check('1: 8 units selected in the expedition window', m and m[-1] == '8', m[-3:] if m else L[-400:])
    check('3: portraits 8 in one row, no overlap', re.search(r'portraits: 8 \(4 stock \+ 4 copies\).*, no overlap', L), S['select_line'])
    check('3: count text says (8/8)', re.search(r"count text now '[^']*\(8/8\)'", L), S['select_line'])
    # ---- supplies step
    log('next', teams(action='stock_next')); time.sleep(3)
    L = dlog()
    shot(out, 'expedition_8_supplies')
    S['supplies_line'] = [l for l in L.splitlines() if 'zetu-suite' in l and 'supplies' in l][-3:]
    st = teams(action='state').get('stock') or {}
    S['stock_panels'] = st.get('panels'); S['stock_state'] = st.get('state')
    check('2: supplies step: 8 panels in one row, no overlap', re.search(r'supplies: 8 unit panels .*, no overlap', L), S['supplies_line'])
    check('2: the window is at the supplies step (no crash)', st.get('state') == 1, st)
    check('2: each of the 8 units has its own panel (GetPanelOwner 0..7)', len({p for p in (st.get('panels') or []) if p}) == 8, st.get('panels'))
    # ---- into the dungeon
    log('accept', teams(action='stock_accept'))
    wait_map(1, 180); time.sleep(8)
    d = units(1)
    S['dungeon_units'] = len(d)
    check('4: 8 units in the dungeon', len(d) == 8, len(d))
    p0 = len(plog())
    log('probe', probe('select_all')); time.sleep(2)
    shot(out, 'dungeon_8_units')
    for t, n in ((8, 'inventory'), (6, 'status'), (7, 'skill')):
        log('probe', probe('toggle %d' % t)); time.sleep(2.5)
        shot(out, 'dungeon_8_' + n)
        log('probe', probe('toggle %d' % t)); time.sleep(1)
    P = plog()[p0:]
    exc = [l for l in P.splitlines() if 'Exception' in l]
    check('5: Inventory / Status / Skill with 8 selected: no exception', not exc, exc[:4])
    # ---- result screen with 8
    log('probe', probe('result')); time.sleep(4)
    L = dlog()
    shot(out, 'results_8')
    S['result_line'] = [l for l in L.splitlines() if 'zetu-suite' in l and 'result' in l][-3:]
    check('6: result screen: 8 portraits, no overlap', re.search(r'result: 8 portraits \(4 stock \+ 4 copies\).*, no overlap', L)
          and re.search(r'result: 8 units \+ 0 dead, 8 portraits shown', L), S['result_line'])
    P = plog()
    S['exceptions'] = [l for l in P.splitlines() if 'Exception' in l][:10]
    check('no exception in player.log during the run', not S['exceptions'], S['exceptions'][:4])
    return S, g0


def main():
    a = sys.argv[1:] or ['run']
    out = os.path.expanduser(a[1] if len(a) > 1 and a[0] in ('run', 'up') else '~/ds_launcher/loader/test_env/l1_out/zetu_ui')
    if a[0] == 'cmd': print(json.dumps(cmd(json.loads(a[1]), 120))); return
    if a[0] == 'probe': print(probe(a[1])); return
    if a[0] == 'req': request(json.loads(a[1])); return
    if a[0] == 'down': open(STOPF, 'w').write('stop\n'); return
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    while True:                                        # the test copy cap first (no pair held while waiting), 90 min at most
        try: r.cap_wait('zetu ui'); break
        except SystemExit:
            if time.time() - t0 > 5400: raise
    lane, pair = lease(); log('lease', pair)
    S = {}; g0 = 0
    try:
        if a[0] == 'up':
            if os.path.exists(STOPF): aside(STOPF, out)
            g0 = up(out); log('UP: copy Q in the world; `zetu_ui_test.py down` to stop')
            t0 = time.time()
            while not os.path.exists(STOPF) and time.time() - t0 < 3 * 3600 and r.alive(r.read_pid(X)): time.sleep(2)
            if os.path.exists(STOPF): aside(STOPF, out)
        else:
            S, g0 = run(out)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        down()
        for f in ('dsload.log', 'player.log', 'guard.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(out, f))
        g = open(os.path.join(I, 'guard.log'), errors='replace').read()[g0:] if os.path.exists(os.path.join(I, 'guard.log')) else ''
        check('guard: never a visible window, never frontmost', 'GUARD KILLED' not in g, g[-300:])
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
        S['checks'] = checks; json.dump(S, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    print(f'\n{len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
