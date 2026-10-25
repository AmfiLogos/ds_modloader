#!/usr/bin/env python3
"""Live building HP (mod live-hp): a selected building's durability number and bar in the bottom panel, and its row in the
Inspect window, follow the building's real durability, on one hidden test copy (default Q; DSL_TEST_COPY). One rig lease.
Never the owner's game, saves or prefs: the world is a copy of ds_multiplayer maps/base_<build>.json (public base map) in <out>.

  live_hp_test.py [stock|mod|both] [out]        stock = without the mod (shows the game's bug), mod = with it; default both
  LIVE_HP_TEST_KIT=<kit dsml folder>            the kit's dsload.dylib + mods (e.g. the built app's Contents/Resources/dsml)
  live_hp_test.py probe '<line>'                hp-probe request to a running copy (pick KEY | select | add V | inspect | state)

The real break the mod run catches: a building that takes damage or gets repaired while it is selected keeps the HP it
had when it was selected, in the bottom panel and in the Inspect window (stock game, every building). hp-probe
(test/hp_probe.c, test only) selects the player's support wagon the game's way (SelectDataContainer + SelectProcessor),
changes its durability with StatusComponent.AddValue and, frame by frame, logs when the panel's number and bar (and the
window's Durability row) first match the real value. The mod run needs a match within 1 s, for damage, for repair, in
the window, and for a second building after the selection moved to it. The stock run must show the bug (no match in 3 s),
else the test proves nothing."""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('DSL_TEST_COPY', 'Q'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
MAP = os.path.join(H, 'ds_multiplayer', 'maps', 'base_25846389.json')
KIT = os.path.expanduser(os.environ.get('LIVE_HP_TEST_KIT', ''))
WAGON, WALL = 'BUILDING_SupportWagonVeryHard', 'BUILDING_RockWall'   # the player's wagon (faction Ally, like built buildings); a rock wall
LIMIT_MS = 1000
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:500]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''
def aside(p, out):
    """never delete: a file / folder of the copy goes to <out>/aside/"""
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))


def probe(line, end, limit=20):
    """one request; the dsload.log lines 'hp-probe: ...' it wrote, up to the line that contains `end`"""
    mark = len(dlog())
    p = os.path.join(I, 'hp_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    t0 = time.time(); L = []
    while time.time() - t0 < limit:
        L = [l.split('hp-probe: ', 1)[1] for l in dlog()[mark:].splitlines() if 'hp-probe: ' in l]
        if any(end in l for l in L): return L
        time.sleep(0.2)
    return L


def num(s):
    """the number before any '/', separators skipped (as the probe reads it); None = none"""
    d = re.sub(r'[^0-9]', '', (s or '').split('/')[0])
    return int(d) if d else None


def state():
    L = probe('state', 'state:')
    m = next((re.search(r"state: panel=(\d) cur='([^']*)' max='([^']*)' fill=(-?[\d.]+) real=(-?[\d.]+)/(-?[\d.]+) insp=(\d) icur='([^']*)' imax='([^']*)'", l) for l in L if l.startswith('state:')), None)
    if not m: return {'raw': L}
    g = m.groups()
    return {'panel': g[0] == '1', 'cur': g[1], 'max': g[2], 'fill': float(g[3]), 'c': float(g[4]), 'm': float(g[5]), 'insp': g[6] == '1',
            'icur': g[7], 'imax': g[8], 'raw': L}


def wait_state(pred, what, timeout=20):
    t0 = time.time(); S = {}
    while time.time() - t0 < timeout:
        S = state()
        if 'c' in S and pred(S): return S
        time.sleep(0.5)
    log('timed out:', what, S.get('raw'))
    return S


def add_case(tag, v, with_mod, window=False):
    """change the picked building's durability by v; the probe logs when the panel (and the window) first matched"""
    L = probe(f'add {v}', 'watch: done', limit=15)
    a = next((l for l in L if l.startswith('add:')), '')
    m = re.search(r'-> dur=(-?[\d.]+)', a)
    check(f'{tag}: the durability changed ({a})', m and 'done' in a and float(m.group(1)) >= 0, L)
    for part in (['panel', 'window'] if window else ['panel']):
        hit = next((re.search(r'matched after (\d+) ms', l) for l in L if l.startswith(f'watch: {part} matched')), None)
        miss = next((l for l in L if l.startswith(f'watch: {part} NOT matched')), None)
        if with_mod:
            check(f'{tag}: the {part} follows within {LIMIT_MS} ms ({hit.group(0) if hit else miss})', hit and int(hit.group(1)) <= LIMIT_MS, L)
        else:
            check(f'{tag}: STOCK BUG shown: the {part} keeps the old value for 3 s ({miss or "it matched"})', miss and not hit, L)


def lease():
    lane = 'livehp-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def mod_src(m):
    if KIT: return os.path.join(KIT, 'mods', m)
    rel = os.path.join(LB, 'build', 'mods_all', 'mac', m)                  # the shipped files when build_mods.sh ran
    return rel if os.path.isdir(rel) else os.path.join(LB, 'build', 'mods', m)


def up(out, with_mod):
    """multiplayer (rig control) + hp-probe (+ live-hp) from this tree's build; copy X started hidden, at the title"""
    if r.alive(r.read_pid(X)): raise SystemExit(f'copy {X} already running')
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    mods = ['multiplayer'] + (['live-hp'] if with_mod else [])
    for m in mods:
        shutil.copytree(mod_src(m), os.path.join(md, m))
    pd = os.path.join(md, 'hp-probe'); os.makedirs(pd)
    subprocess.run(['nice', '-n', '19', 'clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/hp-probe.dylib',
                    '-o', os.path.join(pd, 'hp-probe.dylib'), os.path.join(HERE, 'hp_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'hp-probe.dylib')], capture_output=True)
    json.dump({'id': 'hp-probe', 'name': 'hp probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'hp-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
    en = mods + ['hp-probe']
    for p in ('mp.json', 'dsload.log', 'player.log', 'hp_probe_req.txt', os.path.join('data', 'Saves'), os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods'), 'softrestart'):
        aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True); os.makedirs(os.path.join(I, 'data', 'Mods'))
    prof = os.path.join(out, 'profile.json'); json.dump({'enabled': en, 'order': en}, open(prof, 'w'))
    gp = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gp) if os.path.exists(gp) else 0
    dyl = os.path.join(LB, 'build', 'dsload.dylib')
    if KIT:
        kd = os.path.join(out, 'kit'); os.makedirs(kd, exist_ok=True); shutil.copy2(os.path.join(KIT, 'dsload.dylib'), kd); dyl = os.path.join(kd, 'dsload.dylib')
    os.environ.update({'LRIG_DYLIB': dyl, 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_NO_ALERT': '1', 'DSL_UPDATE': '0'})
    os.environ.pop('DSML_SESSION', None)
    r.start(X)
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    time.sleep(3)
    return g0


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def flow(kind, o):
    with_mod = kind == 'mod'
    L = dlog()
    check(f'{kind}: probe ready', 'hp-probe: ready' in L)
    if with_mod:
        check('mod: live-hp hooks in', "a selected building's HP follows its durability" in L, [l for l in L.splitlines() if 'live-hp' in l][-5:])
    src = os.path.join(o, 'save_in', os.path.basename(MAP)); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(MAP, src)
    log('load', r.send(X, {'cmd': 'load_save', 'file': src}, timeout=60, connect_wait=5))
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
    time.sleep(3)
    P = probe(f'pick {WAGON}', 'pick:')
    check(f'{kind}: the support wagon is in the world', any(l.startswith('pick: ' + WAGON) for l in P), P)
    log('select', probe('select', 'select:'))
    S = wait_state(lambda s: s['panel'] and num(s['cur']) == int(s['c']), 'the panel shows the wagon')
    check(f'{kind}: selected: the panel shows the wagon ({S.get("cur")} / {S.get("max")}, real {S.get("c")})', S.get('panel') and num(S.get('cur')) == int(S.get('c', -9)), S.get('raw'))
    add_case(f'{kind} damage', -1234, with_mod)
    add_case(f'{kind} repair', 600, with_mod)
    log('inspect', probe('inspect', 'inspect:'))
    S = wait_state(lambda s: s['insp'], 'the Inspect window')
    check(f'{kind}: Inspect window open (Durability row {S.get("icur")}, real {S.get("c")})', S.get('insp'), S.get('raw'))
    add_case(f'{kind} damage, Inspect window open', -500, with_mod, window=True)
    # the selection moves to another building: its own numbers, and they follow too
    P = probe(f'pick {WALL}', 'pick:')
    log('select wall', probe('select', 'select:'))
    S = wait_state(lambda s: s['panel'] and num(s['cur']) == int(s['c']), 'the panel shows the wall')
    check(f'{kind}: second building selected: the panel shows it ({S.get("cur")}, real {S.get("c")})', S.get('panel') and num(S.get('cur')) == int(S.get('c', -9)), S.get('raw'))
    add_case(f'{kind} second building damage', -30, with_mod)


def main():
    a = sys.argv[1:] or ['both']
    if a[0] == 'probe': print(probe(a[1], a[1].split()[0], 20)); return
    kinds = {'stock': ['stock'], 'mod': ['mod'], 'both': ['stock', 'mod']}[a[0]]
    out = os.path.expanduser(a[1] if len(a) > 1 else f'~/ds_launcher/loader/test_env/l1_out/live_hp/{time.strftime("%Y%m%d-%H%M%S")}')
    os.makedirs(out, exist_ok=True)
    assert KIT or os.path.exists(os.path.join(LB, 'build', 'dsload.dylib')), 'build the loader first (loader/build.sh), or LIVE_HP_TEST_KIT=<kit dsml folder>'
    t0 = time.time()
    while True:                                        # the test copy cap first (no pair held while waiting), 90 min at most
        try: r.cap_wait('live hp'); break
        except SystemExit:
            if time.time() - t0 > 5400: raise
    lane, pair = lease(); log('lease', pair)
    try:
        for k in kinds:
            g0 = 0; o = os.path.join(out, k); os.makedirs(o, exist_ok=True)
            try:
                g0 = up(o, k == 'mod')
                flow(k, o)
            except Exception as e:
                check(f'{k}: run finished', False, repr(e))
            finally:
                down()
                for f in ('dsload.log', 'dsload.prev.log', 'player.log', 'guard.log'):
                    if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(o, f))
                g = open(os.path.join(I, 'guard.log'), errors='replace').read()[g0:] if os.path.exists(os.path.join(I, 'guard.log')) else ''
                check(f'{k}: guard: never a visible window, never frontmost', 'GUARD KILLED' not in g, g[-300:])
    finally:
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
        json.dump({'checks': checks}, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in checks if not c['ok']]
    for c in checks: print(('PASS ' if c['ok'] else 'FAIL ') + c['name'])
    print(f'\nRESULT {len(checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
