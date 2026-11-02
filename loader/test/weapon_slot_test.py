#!/usr/bin/env python3
"""weapon-slot 0.2 (client only): the 6th bar button shows and uses the equipped weapon's skill, and the mod writes NO game
data. Hidden test copy X (lrig.start: guard, cap, DSL_NOTCH=0, a hidden window so the bar draws), one rig lease, one copy.
The owner's saves and backups are only READ (copied into <out>/save_in first).

  weapon_slot_test.py run [out]       every check below; exit 1 on a failed check
  weapon_slot_test.py cmd '<json>'    rig socket command to the running copy
  env WS_MODS=<dir>                   take weapon-slot from <dir>/weapon-slot (the unzipped release package)
  env WS_COPY=<letter>                the test copy (default X)

Real breaks it catches: a save that is not the game's own (the mod wrote game data: quick slots, skills, auto-use, items),
a weapon button that shows or uses the wrong skill (or uses a skill the unit does not have), a unit without its weapon's
skill whose button is not greyed with the "free a quick slot" hover card, the setting "hide_copy" (local, live) that does
not draw the weapon skill's free slot empty / back, or that changes the game's quick slots or what that slot's key uses,
an exception that stops the bar.
World: a 4-slot world made from the 2026-10-11 backup (make_fill_fixture): units with the weapon skill out of the bar but
still theirs (60c78b46, 477ad25a), units without it (acd20405, 92be08ca: the stock state after an equip with a full bar),
units with free slots. 477ad25a gets one free slot so that a weapon swap gives the new weapon's skill (stock rule): then
the skill sits in a free slot AND on the weapon button (the copy that hide_copy hides on this screen only)."""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('WS_COPY', 'X'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
OLD_SAVE = os.path.join(r.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
ITEMS = os.path.join(H, 'dungeonsettlersdb/data/ItemTable.json')
EQ_UNIT, EQ_ITEM, EQ_SKILL, OLD_SKILL = '477ad25a-7c22-43f5-be28-db055895297e', 'ITEM_WoodClub', 'SKILL_StunCharge', 'SKILL_WhirlwindSlash'
BAD = ('KeyNotFoundException', 'IndexOutOfRangeException', 'ArgumentOutOfRangeException', 'NullReferenceException', 'InvalidCastException')
checks = []
OUT = None


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def rd(p):
    try: return open(p, errors='replace').read()
    except OSError: return ''
def dlog(): return rd(os.path.join(I, 'dsload.log'))
def cmd(o, timeout=60): return r.send(X, o, timeout=timeout, connect_wait=5)
def status(): return cmd({'cmd': 'status'}, 10)
def aside(p, out):                                # never delete: old copy files go to <out>/aside/
    if os.path.lexists(p):
        d = os.path.join(out, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.basename(p) + '.%d' % (time.time() * 1000 % 100000)))
def jload(p): return json.load(open(p, encoding='utf-8-sig'))


def item_skills():                                # item key -> Equipment_ItemDefaultSkills[1] (wiki table, same build)
    d = jload(ITEMS); rows = d if isinstance(d, list) else d.get('rows', d)
    rows = list(rows.values()) if isinstance(rows, dict) else rows
    return {x['Key']: s[1] for x in rows if isinstance(x, dict) for s in [x.get('Equipment_ItemDefaultSkills')] if isinstance(s, list) and len(s) > 1 and s[1] != 'NULL'}
ITEM_W = item_skills()
W = set(ITEM_W.values())


def units_of(save):
    """per unit: quick slots, skill tree (extra skills), skill component (+ auto-use), equipment + inventory keys"""
    d = jload(save); P = d['PlayerUnitsSaveData']; E = d['EntityComponentSaveData']['EntityComponents']
    out = {}
    for g, q in P['QuickSlots'].items():
        c = {e['Type']: e['Data'] for e in E.get(g, [])}
        out[g] = {'quick': q['QuickSlots'], 'tree': P['SkillTrees'].get(g),
                  'skills': {k: c.get(8, {}).get(k) for k in ('BasicSkills', 'ActiveSkills', 'PassiveSkills', 'AutoUse')},
                  'equip': [s.get('Key') for s in c.get(6, {}).get('ItemSlots', [])], 'inv': [s.get('Key') for s in c.get(5, {}).get('ItemSlots', [])]}
    return out
def equipped_skill(u): return next((ITEM_W[k] for k in u['equip'] if k in ITEM_W), None)
def weapon_skill(u):
    """what the weapon button must USE: the equipped weapon's skill when the unit has it, else nothing"""
    w = equipped_skill(u)
    return w if w and w in (u['skills']['ActiveSkills'] or []) else None
def grey_text():
    """the greyed button's hover card in English (T_NOSKILL[0] of the module)"""
    t = open(os.path.join(LB, 'modules', 'weapon-slot', 'weapon_slot.c'), encoding='utf-8').read()
    return re.search(r'T_NOSKILL\[10\] = \{\s*"([^"]+)"', t).group(1)


def make_fill_fixture(dst, free_eq=False):
    """a 4-slot stock world from the 2026-10-11 backup (old layout: weapon skill at 0, 5 slots): the free slots kept, the
    weapon skill out of the bar. acd20405: its weapon skill also out of its skills (equipped with a full bar); 92be08ca:
    also out of its extra skills. free_eq: EQ_UNIT's last slot emptied (room for the swapped weapon's skill).
    Returns {guid: equipped weapon skill}."""
    d = jload(OLD_SAVE); P = d['PlayerUnitsSaveData']; E = d['EntityComponentSaveData']['EntityComponents']
    want = {}
    for g, q in P['QuickSlots'].items():
        c = {e['Type']: e['Data'] for e in E.get(g, [])}
        ew = next((ITEM_W[s.get('Key')] for s in c.get(6, {}).get('ItemSlots', []) if s.get('Key') in ITEM_W), None)
        old = q['QuickSlots']
        q['QuickSlots'] = old[1:5] if len(old) == 5 else old
        q['QuickSlots'] = [None if s == ew else s for s in q['QuickSlots']]
        if free_eq and g == EQ_UNIT: q['QuickSlots'][3] = None
        if ew: want[g] = ew
        if g[:8] in ('acd20405', '92be08ca') and ew:
            sk = c[8]
            sk['ActiveSkills'] = [s for s in sk.get('ActiveSkills') or [] if s != ew]
            for k in ('AutoUse', 'Cooldowns', 'SkillDurations'):
                if isinstance(sk.get(k), dict): sk[k].pop(ew, None)
            if g[:8] == '92be08ca': P['SkillTrees'][g]['ExtraSkills'] = [s for s in P['SkillTrees'][g]['ExtraSkills'] if s != ew]
    json.dump(d, open(dst, 'w'))
    return want


def save_now(name):
    p = os.path.join(I, 'data', 'Saves', name + '.json')
    aside(p, OUT)
    cmd({'cmd': 'ord_save', 'name': name})
    for _ in range(60):                           # kept in <out>: the game deletes older saves of the same world
        if os.path.exists(p) and os.path.getsize(p) > 1000:
            time.sleep(1); k = os.path.join(OUT, 'saves', name + '.json'); os.makedirs(os.path.dirname(k), exist_ok=True)
            aside(k, OUT); shutil.copy2(p, k); return k
        time.sleep(1)
    return None


def errors(mark):
    """exception lines in player.log / ErrorLog since the start of this run"""
    t = rd(os.path.join(I, 'player.log')) + rd(os.path.join(I, 'data', 'ErrorLog.txt'))[mark:]
    return [l for l in t.splitlines() if any(b in l for b in BAD)]


def build_probe(md):
    """test-only module multibar_probe.c: logs UseSkillModeContext.AddUseSkill (unit, skill) and the bars' shown keys"""
    pd = os.path.join(md, 'multibar-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/multibar-probe.dylib',
                    '-o', os.path.join(pd, 'multibar-probe.dylib'), os.path.join(HERE, 'multibar_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'multibar-probe.dylib')], capture_output=True)
    json.dump({'id': 'multibar-probe', 'name': 'multibar probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'multibar-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)


def mods_dir(out, enabled, probe=True):
    """<out>/mods with the mods of this tree (WS_MODS: the shipped weapon-slot) + the probe; the profile; returns (md, prof)"""
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    for m in enabled:
        src = os.path.join(os.environ['WS_MODS'], m) if m == 'weapon-slot' and os.environ.get('WS_MODS') else os.path.join(LB, 'build', 'mods', m)
        shutil.copytree(src, os.path.join(md, m))
    if probe: build_probe(md); enabled = list(enabled) + ['multibar-probe']
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': list(enabled), 'order': list(enabled)}, open(prof, 'w'))
    return md, prof


def up(out, enabled):
    if r.alive(r.read_pid(X)): raise SystemExit('copy %s already running' % X)
    md, prof = mods_dir(out, enabled)
    for p in ('mp.json', 'dsload.log', 'player.log', 'multibar_probe_req.txt', os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups'), os.path.join('data', 'Mods', 'settings.json')):
        aside(os.path.join(I, p), out)                # settings.json: every mod setting at its default (hide_copy off)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    for k in ('DSML_ROOT', 'DSML_APP', 'DSML_SESSION', 'DSML_GAME_SAVES', 'DSRIG_DIAG'): os.environ.pop(k, None)
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1',
                       'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_CRASHREP': '0', 'DSL_UPDATE': '0', 'DSRIG_BG': '1'})
    os.environ.update(r.soft_test_env())
    mark = len(rd(os.path.join(I, 'data', 'ErrorLog.txt')))
    r.start(X, window=True)
    r.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    return mark


def load(src, out):
    dst = os.path.join(out, 'save_in', os.path.basename(src)); os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.abspath(src) != os.path.abspath(dst): shutil.copy2(src, dst)
    log('load', os.path.basename(src), cmd({'cmd': 'load_save', 'file': dst}))
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
    cmd({'cmd': 'set_speed', 'speed': 0})
    time.sleep(3)


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def inject(ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return cmd(o)


def probe(line, want, limit=20):
    """one request to the probe (multibar_probe_req.txt); its new lines once one contains `want`"""
    mark = len(dlog())
    p = os.path.join(I, 'multibar_probe_req.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
    t0 = time.time()
    while time.time() - t0 < limit:
        L = [l for l in dlog()[mark:].splitlines() if 'multibar-probe: ' in l]
        if any(want in l for l in L): return L
        time.sleep(0.25)
    return []


WBTN = {}                                         # the real bar's weapon button at the last real_bar(): act, hover, type, tip
def real_bar():
    """the real skill bar: (shown unit, slot count, shown keys: 0 = basic, 1..4 free slots, 5 = the weapon button)"""
    L = probe('state', 'multibar-probe: state: ')
    WBTN.clear()
    for m in re.findall(r'wbtn \[(\S+) act=(-?\d+) hover=(\d) type=(-?\d+) tip=(.*)\]', '\n'.join(L)):
        if not m[0].startswith('QuickSlotUI_Multi'): WBTN.update(act=int(m[1]), hover=int(m[2]), type=int(m[3]), tip=m[4])
    for m in re.findall(r'bar \[(\S+) active=(\d) slots=(\d+) unit=(\S+) keys=(\S*)\]', '\n'.join(L)):
        if not m[0].startswith('QuickSlotUI_Multi'): return m[3], int(m[2]), m[4].split(',')
    return None, 0, []


def show(g, mp):
    """select unit g the game's way (a portrait click): the bar shows it"""
    inject('SelectPlayerUnitRequested', i32_16=-1, guid_20=g)
    t0 = time.time()
    while time.time() - t0 < 10:
        u, n, keys = real_bar()
        if u == g: return n, keys
        time.sleep(0.7)
    return real_bar()[1:]


def request(o):                                   # loader test request (DSL_SOFT_TEST=1): a Mods settings page value
    d = os.path.join(I, 'softrestart'); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'reg_request.json.tmp'), 'w').write(json.dumps(o)); os.replace(os.path.join(d, 'reg_request.json.tmp'), os.path.join(d, 'reg_request.json'))
def hide_copy(on):
    """the setting hide_copy, set as on the Mods settings page (local: live, no restart); True when the module took it"""
    dm = len(dlog()); request({'cmd': 'setting', 'mod': 'weapon-slot', 'id': 'hide_copy', 'value': bool(on)})
    t0 = time.time()
    while time.time() - t0 < 15:
        if 'setting hide_copy: %s' % ('on' if on else 'off') in dlog()[dm:]: time.sleep(1); return True
        time.sleep(0.5)
    return False
def shot(name):                                   # env WS_SHOTS=<dir>: a picture of the screen for the lead (not a check)
    if os.environ.get('WS_SHOTS'):
        os.makedirs(os.environ['WS_SHOTS'], exist_ok=True)
        log('shot', name, cmd({'cmd': 'ui_capture', 'path': os.path.join(os.environ['WS_SHOTS'], name)}).get('ok'))


def use_button(g, i=5):
    """bar slot i (UseQuickSlotSkillRequested Index i, the stock click / key path; 5 = the weapon button): the skills
    AddUseSkill got for unit g"""
    dm = len(dlog())
    inject('UseQuickSlotSkillRequested', i32_16=i); time.sleep(2)
    adds = [l.split('AddUseSkill ', 1)[1].split()[:2] for l in dlog()[dm:].splitlines() if 'multibar-probe: AddUseSkill ' in l]
    cmd({'cmd': 'ord_inject', 'ev': 'BaseControlModeRequested'})   # leave the aiming mode
    return [a[1] for a in adds if a[0] == g], adds


def run(out):
    src = os.path.join(out, 'save_in', 'ws_world.json'); os.makedirs(os.path.dirname(src), exist_ok=True)
    make_fill_fixture(src, free_eq=True)
    before = units_of(src)
    mark = up(out, ['multiplayer', 'weapon-slot'])
    L = dlog()
    check('module on: client only, 7 hooks', 'weapon skill slot on (client only: 7 hooks, no game data written; hide the copy: off)' in L,
          [l for l in L.splitlines() if 'weapon-slot' in l][-6:])
    if not checks[-1]['ok']: raise SystemExit('module not on: stop')
    load(src, out)
    wl = [l for l in dlog().splitlines() if 'weapon skills (' in l]
    mp = status().get('map', 0)
    on_map = [u['guid'] for u in cmd({'cmd': 'list_units'}).get('units') or [] if u.get('faction') == 1 and 'cell' in u]
    exp = {g: weapon_skill(u) for g, u in before.items() if g in on_map}
    log('units on the map, expected weapon button:', {g[:8]: w for g, w in exp.items()})
    check('the test world has units WITH the weapon skill out of the bar and units WITHOUT it',
          any(w and w not in before[g]['quick'] for g, w in exp.items()) and any(not w for g, w in exp.items() if any(k in ITEM_W for k in before[g]['equip'])),
          {g[:8]: (before[g]['quick'], w) for g, w in exp.items()})
    grey = grey_text()
    for g, w in exp.items():
        n, keys = show(g, mp)
        shown = keys[5] if len(keys) > 5 and keys[5] != '-' else None
        eq = equipped_skill(before[g])
        if w or not eq:
            check('unit %s: the bar has 6 slots, the weapon button shows %s' % (g[:8], w or 'nothing'),
                  n == 6 and shown == w and not (w and WBTN.get('tip')), (n, keys, WBTN))
        else:                                     # (b) the unit does not have its weapon's skill (stock: equipped with a full bar)
            check('unit %s: the weapon button shows %s GREYED with the "free a quick slot" hover card' % (g[:8], eq),
                  n == 6 and shown == eq and WBTN.get('act') == 0 and WBTN.get('hover') == 1 and WBTN.get('tip') == grey, (n, keys, WBTN))
            if not any(c['name'].startswith('hover card shown') for c in checks):
                dm = len(dlog()); probe('hoverw 1', 'hoverw 1:'); time.sleep(1.5); shot('ws_b_greyed_hover.png')
                check('hover card shown on the greyed button (TooltipFeedback.OnInteractStart)', 'hoverw 1: done' in dlog()[dm:], dlog()[dm:][-300:])
                probe('hoverw 0', 'hoverw 0:')
        used, adds = use_button(g)
        check('unit %s: the weapon button uses %s' % (g[:8], w or 'nothing'), used == ([w] if w else []), adds)
    if wl: log(wl[0])
    p = save_now('ws_round'); after = units_of(p) if p else {}
    diff = {g[:8]: [k for k in ('quick', 'tree', 'skills', 'equip', 'inv') if after.get(g, {}).get(k) != u[k]] for g, u in before.items()}
    diff = {g: d for g, d in diff.items() if d}
    check('the save is the game\'s own: quick slots (4), skill trees, skills + auto-use, items unchanged by the mod',
          after and set(after) == set(before) and not diff, diff or len(after))
    # weapon swap (the game's own equip order): the button follows the new weapon; the game's quick slots are stock
    sv = before[EQ_UNIT]; idx = sv['inv'].index(EQ_ITEM) if EQ_ITEM in sv['inv'] else -1
    if EQ_UNIT in exp and idx >= 0:
        log('equip', inject('EquipItemRequested', i32_16=mp, guid_20=EQ_UNIT, i32_36=0, guid_40=EQ_UNIT, i32_56=idx)); time.sleep(3)
        q = cmd({'cmd': 'ord_read', 'guid': EQ_UNIT}).get('quick')
        check('swap: the game puts %s in its first empty free slot (stock, 4 slots)' % EQ_SKILL, q and len(q) == 4 and q[3] == EQ_SKILL, q)
        n, keys = show(EQ_UNIT, mp)
        check('swap: the weapon button shows %s' % EQ_SKILL, n == 6 and len(keys) > 5 and keys[5] == EQ_SKILL, keys)
        used, adds = use_button(EQ_UNIT)
        check('swap: the weapon button uses %s' % EQ_SKILL, used == [EQ_SKILL], adds)
        # (a) hide_copy: EQ_SKILL now sits in free slot 4 (UI 4) AND on the weapon button. View only: slot 4 drawn empty,
        # the game's quick slots unchanged, slot 4's key still uses the skill; switched off, the slot is drawn again
        check('hide_copy on: the module takes the Mods page value at once', hide_copy(True))
        _, n, keys = real_bar()
        q2 = cmd({'cmd': 'ord_read', 'guid': EQ_UNIT}).get('quick')
        check('hide_copy on: free slot 4 drawn empty, the weapon button shows %s, the game\'s quick slots unchanged' % EQ_SKILL,
              len(keys) > 5 and keys[4] == '-' and keys[5] == EQ_SKILL and q2 == q, (keys, q2))
        shot('ws_a_hide_copy_on.png')
        used, adds = use_button(EQ_UNIT, 4)
        check('hide_copy on: the key of free slot 4 still uses %s (game data)' % EQ_SKILL, used == [EQ_SKILL], adds)
        other = next((g for g in exp if g != EQ_UNIT), None)
        if other: show(other, mp)
        n, keys = show(EQ_UNIT, mp)
        check('hide_copy on: still hidden after the bar is rebuilt (select another unit and back)', len(keys) > 5 and keys[4] == '-' and keys[5] == EQ_SKILL, keys)
        check('hide_copy off: the module takes it at once', hide_copy(False))
        _, n, keys = real_bar()
        check('hide_copy off: free slot 4 shows %s again' % EQ_SKILL, len(keys) > 5 and keys[4] == EQ_SKILL and keys[5] == EQ_SKILL, keys)
    else:
        check('swap unit %s on the map with %s in its bag' % (EQ_UNIT[:8], EQ_ITEM), False, (EQ_UNIT in exp, idx))
    check('no exception', not errors(mark), errors(mark)[:5])
    down()


def lease():
    lane = 'dswslot-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def main():
    global OUT
    if len(sys.argv) > 1 and sys.argv[1] == 'cmd':
        print(json.dumps(cmd(json.loads(sys.argv[2])), indent=1)); return 0
    OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(LB, 'test_env', 'weapon_slot'))
    os.makedirs(OUT, exist_ok=True)
    if r.owner_game_running(): log('note: the owner\'s game runs (one hidden copy, nice, background)')
    lane, pair = lease()
    try:
        run(OUT)
    finally:
        down()
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(checks, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(checks) - len(bad), len(checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
