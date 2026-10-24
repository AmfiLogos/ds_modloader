#!/usr/bin/env python3
"""Multi-unit view (mod id multi-bar) on hidden test copy W (lrig.start: guard, cap, DSL_NOTCH=0; a hidden window so the UI
draws; DSRIG_BG=1). One rig lease. The world: the wiki's public base save (4 player units), copied into <out> and loaded.
The probe module (multibar_probe.c, test only) sends the game's own selection events and presses the cards' slot buttons.

  multibar_test.py run [out]        the whole proof; exit 1 on a failed check
  multibar_test.py probe '<line>'   one probe request to the running copy (sel i | add i | click k s | state)
  multibar_test.py up [out] / down  start + load and hold (manual look) / stop

Real breaks the run catches:
 1  clicking card k's slot uses that slot for unit k, not for the first selected unit (2, 3 and 4 units selected):
    the game's UseSkillModeContext.AddUseSkill gets exactly unit k (basic attack slot 0 and the quick-slot skill slot 1)
 2  selection still works after the module's hooks: Shift-select (add / remove) changes the cards, one card per unit,
    in party order; back to one unit = the stock bar for that unit and the summary card's body back with its tabs
 3  no game exception in player.log, the copy never shows a window"""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = 'W'; I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
SAVE = os.path.join(H, 'dungeonsettlersdb', 'site', 'assets', 'saves', 'base_25846389.json')
STOPF = os.path.join(I, 'multibar_stop')
ZERO = '00000000-0000-0000-0000-000000000000'
WITH = tuple(m for m in os.environ.get('MULTIBAR_WITH', '').split(',') if m)   # other mods on in the run, e.g. weapon-slot
WS = 'weapon-slot' in WITH                         # the weapon slot = bar slot 5 (quick slot 4), shown after the basic attack
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:500]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
class MacT:                                        # the test copy on this Mac (win/multibar_vm.py: the Windows VM)
    def dlog(self):
        try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
        except OSError: return ''
    def plog(self):
        try: return open(os.path.join(I, 'player.log'), errors='replace').read()
        except OSError: return ''
    def req(self, line):
        p = os.path.join(I, 'multibar_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    def gone(self):
        return None if r.alive(r.read_pid(X)) else open(os.path.join(I, 'guard.log'), errors='replace').read()[-200:]
    def load_world(self, out):
        src = os.path.join(out, 'save_in', 'MultiBarTest__ManualSave_0.json'); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(SAVE, src)
        log('load', cmd({'cmd': 'load_save', 'file': src}))
        r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
T = MacT()
def dlog(): return T.dlog()
def plog(): return T.plog()
def cmd(o, timeout=60): return r.send(X, o, timeout=timeout, connect_wait=5)
def status(): return cmd({'cmd': 'status'}, 10)
def probe(line, want='multibar-probe: ', limit=20):
    """send one request; the new probe lines once one contains `want`"""
    mark = len(dlog())
    T.req(line)
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l for l in dlog()[mark:].splitlines() if 'multibar-probe: ' in l]
        if any(want in l for l in L): return L
        g = T.gone()
        if g is not None: raise RuntimeError('the test copy is gone (crash?): ' + g)
        time.sleep(0.25)
    return []


def state():
    L = probe('state', 'multibar-probe: state: ')
    s = next((l for l in L if 'state: ' in l), '')
    bars = [dict(name=m[0], active=int(m[1]), slots=int(m[2]), unit=m[3], keys=m[4].split(','))
            for m in re.findall(r'bar \[(\S+) active=(\d) slots=(\d+) unit=(\S+) keys=(\S*)\]', '\n'.join(L))]
    s = ' '.join('[%s %s %s]' % (b['name'], b['unit'][:8], ','.join(b['keys'])) for b in bars) + ' ' + s
    body = re.search(r'summary_body=(-?\d) tabs_in_body=(-?\d)', s)
    real = [b for b in bars if not b['name'].startswith('QuickSlotUI_Multi')]
    cards = sorted((b for b in bars if b['name'].startswith('QuickSlotUI_Multi') and b['active']), key=lambda b: b['name'])
    return dict(real=real[0] if real else None, cards=[c['unit'] for c in cards], card_slots=[c['slots'] for c in cards],
                card_keys=[c['keys'] for c in cards],
                body=int(body.group(1)) if body else None, tabs_in_body=int(body.group(2)) if body else None, raw=s[-600:])


def wait_state(pred, limit=10):
    t0 = time.time(); s = state()
    while not pred(s) and time.time() - t0 < limit: time.sleep(0.7); s = state()
    return s


def click_units(k, s, settle=2.5):
    """press card k's slot s; the units AddUseSkill got afterwards (and the module's line)"""
    mark = len(dlog())
    probe('click %d %d' % (k, s), 'click %d %d:' % (k, s))
    t0 = time.time(); last = None
    while time.time() - t0 < 8:
        L = dlog()[mark:]
        n = L.count('AddUseSkill ')
        if n and n == last: break
        last = n; time.sleep(settle / 2)
    L = dlog()[mark:]
    adds = re.findall(r'multibar-probe: AddUseSkill (\S+) (\S*)', L)
    mod = re.findall(r'multi-bar: card (\d+) slot (\d+): ([^\n]+)', L)
    return adds, mod


def aside(p, out):
    """never delete: a file / folder of the copy goes to <out>/aside/"""
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))


def lease():
    lane = 'multibar-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def up(out):
    """mods of this tree + the probe; copy W started hidden; the base save loaded"""
    if r.alive(r.read_pid(X)): raise SystemExit('copy W already running')
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    src = os.environ.get('MULTIBAR_MODS') or os.path.join(LB, 'build', 'mods')   # e.g. build/mods_all/mac = the files that ship
    for m in ('multiplayer', 'multi-bar') + WITH: shutil.copytree(os.path.join(src, m), os.path.join(md, m))
    pd = os.path.join(md, 'multibar-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/multibar-probe.dylib',
                    '-o', os.path.join(pd, 'multibar-probe.dylib'), os.path.join(HERE, 'multibar_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'multibar-probe.dylib')], capture_output=True)
    json.dump({'id': 'multibar-probe', 'name': 'multibar probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'multibar-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
    en = ['multiplayer'] + list(WITH) + ['multi-bar', 'multibar-probe']
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': en, 'order': en}, open(prof, 'w'))
    for p in ('mp.json', 'dsload.log', 'player.log', 'multibar_probe_req.txt', os.path.join('data', 'Saves'), os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    gp = os.path.join(I, 'guard.log'); g0 = os.path.getsize(gp) if os.path.exists(gp) else 0
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_NO_ALERT': '1', 'DSRIG_BG': '1'})
    os.environ.pop('DSML_SESSION', None)
    r.start(X, window=True)
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    L = dlog()
    check('module ready (every name found, 17 hooks)', 'multi-bar: ready (17 hooks' in L, [l for l in L.splitlines() if 'multi-bar' in l][-6:])
    check('probe ready', 'multibar-probe: ready' in L, [l for l in L.splitlines() if 'probe' in l][-3:])
    T.load_world(out)
    return g0


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def run(out):
    g0 = up(out)
    return flow(), g0


def flow():
    """the checks, on a copy in the world with the base save (Mac: up(); Windows: win/multibar_vm.py)"""
    S = {}
    time.sleep(3)
    # ---- party order: one unit at a time = the stock bar, its unit = party unit i
    party = []
    for i in range(4):
        probe('sel %d' % i, 'sel %d:' % i)
        s = wait_state(lambda s: s['real'] and s['real']['active'] and not s['cards'] and (not party or s['real']['unit'] != party[-1]))
        party.append(s['real']['unit'] if s['real'] else ZERO)
    S['party'] = party
    check('one unit selected: the stock bar, no card (4 units)', len(set(party)) == 4 and ZERO not in party, party)
    real_slots = (state()['real'] or {}).get('slots')
    # ---- 2, 3, 4 units by Shift-select; a click on card k uses unit k
    probe('sel 0', 'sel 0:')
    wait_state(lambda s: s['real'] and s['real']['unit'] == party[0])
    for n in (2, 3, 4):
        probe('add %d' % (n - 1), 'add %d:' % (n - 1))
        s = wait_state(lambda s: len(s['cards']) == n)
        S['state_%d' % n] = s
        check('%d units (Shift-select): %d cards in party order' % (n, n), s['cards'] == party[:n], s['raw'])
        check('%d units: the cards have the real bar\'s slots (%s)' % (n, real_slots), s['card_slots'] and all(x == real_slots for x in s['card_slots']), s['card_slots'])
        check('%d units: summary card body hidden, its tabs kept' % n, s['body'] == 0 and s['tabs_in_body'] == 0, s['raw'][-80:])
        if WS:
            ks = s['card_keys']
            check('%d units, weapon slot on: every card has 6 slots and a weapon skill in slot 5' % n,
                  len(ks) == n and all(len(x) == 6 and x[5] != '-' for x in ks), ks)
        for k in sorted({0, n - 1, n // 2}):
            keys = s['card_keys'][k] if k < len(s['card_keys']) else []
            skill_slots = [5] if WS else []
            skill_slots += [j for j in range(1, min(len(keys), 5)) if keys[j] != '-'][:1] or ([] if WS else [1])
            for slot in [0] + skill_slots:
                adds, mod = click_units(k, slot)
                users = [a[0] for a in adds]
                want = keys[slot] if slot > 0 and slot < len(keys) and keys[slot] != '-' else None
                S['click_%d_%d_%d' % (n, k, slot)] = {'adds': adds, 'mod': mod, 'slot_key': want}
                what = 'weapon slot' if WS and slot == 5 else 'slot %d' % slot
                check('%d units: card %d %s uses unit %d only%s' % (n, k, what, k, (' (' + want + ')') if want else ''),
                      users == [party[k]] and (want is None or [a[1] for a in adds] == [want]), (adds, mod))
    # ---- Shift-select removes a unit again; then one unit = stock
    probe('add 3', 'add 3:')
    s = wait_state(lambda s: len(s['cards']) == 3)
    check('Shift-select removes unit 3: 3 cards', s['cards'] == party[:3], s['raw'])
    probe('sel 2', 'sel 2:')
    s = wait_state(lambda s: not s['cards'] and s['real'] and s['real']['active'] and s['real']['unit'] == party[2])
    check('back to one unit: stock bar for that unit, no card', not s['cards'] and s['real'] and s['real']['active'] == 1 and s['real']['unit'] == party[2], s['raw'])
    check('back to one unit: summary card body and its tabs back', s['body'] == 1 and s['tabs_in_body'] == 1, s['raw'][-80:])
    adds, mod = click_units(0, 0)
    check('no card to click with one unit', not adds and not mod, (adds, mod))
    # ---- control: the STOCK path (one unit, the skill key) for unit 0's slot-1 skill. A self-target skill is cast at the
    # mouse at once (stock rule, same in the module); in a hidden copy the mouse is off the map and the game's
    # TaskSystem.ForceTickDirectOrder throws. Those errors are the environment when the stock key gives them too.
    def exc(): return re.findall(r'(\w+Exception)[^\n]*\n\s+at ([\w.]+)', plog())
    probe('sel 0', 'sel 0:'); s = wait_state(lambda s: s['real'] and s['real']['unit'] == party[0])
    rk = (s['real'] or {}).get('keys') or []
    ck = next((j for j in ([5] if WS else []) + list(range(1, len(rk))) if j < len(rk) and rk[j] != '-'), 1)
    n0 = len(exc()); mark = len(dlog())
    probe('key %d' % ck, 'key %d:' % ck); time.sleep(3)
    stock_adds = re.findall(r'multibar-probe: AddUseSkill (\S+) (\S*)', dlog()[mark:])
    stock_exc = exc()[n0:]
    S['stock_key'] = {'adds': stock_adds, 'exc': stock_exc}
    check('control: the stock key uses unit 0\'s slot-%d skill' % ck, [a[0] for a in stock_adds] == [party[0]], stock_adds)
    env = 'Refactor.Tick.TaskSystem.ForceTickDirectOrder'
    stock_env = any(e[1] == env for e in stock_exc)
    S['exceptions'] = exc()
    other = [e for e in S['exceptions'] if not (stock_env and e[1] == env)]
    log('exceptions: %d, from %s: %d (stock key gave it too: %s)' % (len(S['exceptions']), env, len(S['exceptions']) - len(other), stock_env))
    check('no exception in player.log (except the one the stock key gives in a hidden copy)', not other, other[:4])
    return S


def main():
    a = sys.argv[1:] or ['run']
    out = os.path.expanduser(a[1] if len(a) > 1 and a[0] in ('run', 'up') else '~/ds_launcher/loader/test_env/l1_out/multibar')
    if a[0] == 'probe': print('\n'.join(probe(a[1]))); return
    if a[0] == 'down': open(STOPF, 'w').write('stop\n'); return
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    while True:                                        # the test copy cap first (no pair held while waiting), 90 min at most
        try: r.cap_wait('multibar'); break
        except SystemExit:
            if time.time() - t0 > 5400: raise
    lane, pair = lease(); log('lease', pair)
    S = {}; g0 = 0
    try:
        if a[0] == 'up':
            if os.path.exists(STOPF): aside(STOPF, out)
            g0 = up(out); log('UP: copy W in the world; `multibar_test.py down` to stop')
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
