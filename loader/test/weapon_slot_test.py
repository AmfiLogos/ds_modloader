#!/usr/bin/env python3
"""weapon-slot module: saves both ways (mod on / off), the 6-slot bar, equip, and the weapon slot that game events
cannot change. Hidden test copy X (lrig.start: guard, cap, DSL_NOTCH=0, a hidden window so the bar draws), one rig
lease, one copy at a time. The owner's saves and backups are only READ (copied into <out>/save_in first).

  weapon_slot_test.py run [out]       every step below; exit 1 on a failed check
  weapon_slot_test.py cmd '<json>'    rig socket command to the running copy
  env WS_MODS=<dir>                   take weapon-slot from <dir>/weapon-slot (the unzipped release package)
  env WS_STEPS=e,d,f                  the steps to run (default all); WS_COPY=<letter> the test copy (default X)
  env WS_E1_SAVE=<file>               E1's world (default: the owner's Elven Expedition autosave, copied)

Real breaks it catches (a save the stock game cannot read, data loss, an exception that stops the bar, a slot that the
game or the player changes, a weapon skill that no longer casts). Saves are written in stock form since 0.1.2.
 E1 mod on, a COPY of the owner's world (5-slot save of 0.1.1, two units with Whirlwind Slash auto-use off): every
    unit gets its weapon slot from slot 4; the save written has 4 slots per unit, the weapon skill in the first empty
    free slot or (4 free slots full) in no slot, and nothing else of the units changes (skill trees, extra skills,
    skills with their auto-use choices, items); after the save the live bars still have the weapon slot.
 E2 the game WITHOUT this mod (loader + multiplayer as the test driver, weapon-slot off), the save of E1: the world
    loads with the saved 4-slot bars and the game writes it back unchanged (bars, skill trees, skills, items).
 E3 mod on again, the 4-slot save of E2: the live bars are those of E1 (weapon skill out of its free slot into the
    weapon slot, empty weapon slots filled from the weapons at load end) and the save equals E1's (stable round
    trip). A Set to index 4 (a drop on the weapon slot) changes nothing; the weapon skill set into a free slot keeps
    it in the weapon slot too; bar slot 5 (UseQuickSlotSkill Index 5) makes the game use the unit's weapon skill
    (UseSkillModeContext.AddUseSkill, probe module multibar_probe.c), with no exception.
 D  mod on, a save of the old binary mod (weapon skill at index 0, 2026-10-11 backup, 4 units in a dungeon): turned
    into the new layout and saved in stock form; that save loads again with the weapon slots; equip a WoodClub over a
    greatsword: the weapon slot holds StunCharge, the free slots do not change, the unit has StunCharge and no longer
    WhirlwindSlash; each unit's weapon skill is ordered at the closest enemy and the game runs: a weapon skill got a
    cooldown (it was cast).
 F  fill on load (0.1.1), a 4-slot world made from the backup where weapon skills are missing: from the bar only
    (a world played without the mod), also from the unit's skills (equipped while the 4 free slots were full), also
    from its extra skills. After the load every armed unit has its weapon's skill in the weapon slot, once in its skills
    and once in its extra skills; the save is in stock form; no exception."""
import json, math, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('WS_COPY', 'X'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
OWNER_SAVE = os.environ.get('WS_E1_SAVE') or os.path.join(H, 'Library/Application Support/CanOpener/Dungeon Settlers/Saves',
                          'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')   # 5-slot of 0.1.1 (read only: copied)
OLD_SAVE = os.path.join(r.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
ITEMS = os.path.join(H, 'dungeonsettlersdb/data/ItemTable.json')
EQ_UNIT, EQ_ITEM, EQ_SKILL, OLD_SKILL = '477ad25a-7c22-43f5-be28-db055895297e', 'ITEM_WoodClub', 'SKILL_StunCharge', 'SKILL_WhirlwindSlash'
BAD = ('KeyNotFoundException', 'IndexOutOfRangeException', 'ArgumentOutOfRangeException', 'NullReferenceException')
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def rd(p):
    try: return open(p, errors='replace').read()
    except OSError: return ''
def dlog(): return rd(os.path.join(I, 'dsload.log'))
def cmd(o, timeout=60): return r.send(X, o, timeout=timeout, connect_wait=5)
def status(): return cmd({'cmd': 'status'}, 10)
def request(o):                                   # loader test request (DSL_SOFT_TEST=1): answers the save warning
    d = os.path.join(I, 'softrestart'); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'reg_request.json.tmp'), 'w').write(json.dumps(o)); os.replace(os.path.join(d, 'reg_request.json.tmp'), os.path.join(d, 'reg_request.json'))
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
    """per unit: quick slots, extra skills, skill component, equipment + inventory keys"""
    d = jload(save); P = d['PlayerUnitsSaveData']; E = d['EntityComponentSaveData']['EntityComponents']
    out = {}
    for g, q in P['QuickSlots'].items():
        c = {e['Type']: e['Data'] for e in E.get(g, [])}
        out[g] = {'quick': q['QuickSlots'], 'tree': P['SkillTrees'].get(g),
                  'skills': {k: c.get(8, {}).get(k) for k in ('BasicSkills', 'ActiveSkills', 'PassiveSkills', 'AutoUse')},
                  'cooldowns': c.get(8, {}).get('Cooldowns') or {},
                  'equip': [s.get('Key') for s in c.get(6, {}).get('ItemSlots', [])], 'inv': [s.get('Key') for s in c.get(5, {}).get('ItemSlots', [])]}
    return out


def expect_new(u, old_layout=False):
    """the module's rules on one unit of a save file: migration, then the fill of an empty weapon slot at load end"""
    q = list(u['quick']) + [None] * (5 - len(u['quick']))
    w = next((s for s in (u['tree'] or {}).get('ExtraSkills') or [] if s in W), None)
    if not old_layout:
        if len(u['quick']) < 5 and w and w in q[:4]: q = [None if s == w else s for s in q[:4]] + [w]
    elif w and q[0] == w and q[4] != w: q = q[1:5] + [w]
    ew = next((ITEM_W[k] for k in u['equip'] if k in ITEM_W), None)
    if not q[4] and ew: q = [None if s == ew else s for s in q[:4]] + [ew]
    return q


def stock_bar(q):
    """the 4 slots the stock game keeps for a live 5-slot bar: the weapon skill in its free slot if there, else in the
    first empty one (TryRegisterQuickSlot), else in none"""
    s, w = list(q[:4]), q[4]
    if w and w not in s:
        k = next((i for i in range(4) if not s[i]), None)
        if k is not None: s[k] = w
    return s


def make_fill_fixture(dst):
    """a 4-slot world from the 2026-10-11 backup (old layout): the free slots kept, the weapon slot cut (a world played
    without the mod). acd20405: its weapon skill also out of its skills (equipped with a full bar); 92be08ca: also out
    of its extra skills. Returns {guid: equipped weapon skill}."""
    d = jload(OLD_SAVE); P = d['PlayerUnitsSaveData']; E = d['EntityComponentSaveData']['EntityComponents']
    want = {}
    for g, q in P['QuickSlots'].items():
        c = {e['Type']: e['Data'] for e in E.get(g, [])}
        ew = next((ITEM_W[s.get('Key')] for s in c.get(6, {}).get('ItemSlots', []) if s.get('Key') in ITEM_W), None)
        old = q['QuickSlots']
        q['QuickSlots'] = old[1:5] if len(old) == 5 else old
        q['QuickSlots'] = [None if s == ew else s for s in q['QuickSlots']]
        if ew: want[g] = ew
        if g[:8] in ('acd20405', '92be08ca') and ew:
            sk = c[8]
            sk['ActiveSkills'] = [s for s in sk.get('ActiveSkills') or [] if s != ew]
            for k in ('AutoUse', 'Cooldowns', 'SkillDurations'):
                if isinstance(sk.get(k), dict): sk[k].pop(ew, None)
            if g[:8] == '92be08ca': P['SkillTrees'][g]['ExtraSkills'] = [s for s in P['SkillTrees'][g]['ExtraSkills'] if s != ew]
    json.dump(d, open(dst, 'w'))
    return want


def live(guids):
    """units on the shown map only: ord_read finds no unit of another map (the save file check covers every unit)"""
    out = {g: cmd({'cmd': 'ord_read', 'guid': g}).get('quick') for g in guids}
    return {g: q for g, q in out.items() if q is not None}


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


OUT = None
def up(out, enabled, probe=False):
    if r.alive(r.read_pid(X)): raise SystemExit('copy %s already running' % X)
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    for m in enabled:                             # WS_MODS = a folder with the shipped weapon-slot (unzipped package)
        src = os.path.join(os.environ['WS_MODS'], m) if m == 'weapon-slot' and os.environ.get('WS_MODS') else os.path.join(LB, 'build', 'mods', m)
        shutil.copytree(src, os.path.join(md, m))
    if probe:                                     # test-only module: logs UseSkillModeContext.AddUseSkill (unit, skill)
        pd = os.path.join(md, 'multibar-probe'); os.makedirs(pd)
        subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/multibar-probe.dylib',
                        '-o', os.path.join(pd, 'multibar-probe.dylib'), os.path.join(HERE, 'multibar_probe.c')], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'multibar-probe.dylib')], capture_output=True)
        json.dump({'id': 'multibar-probe', 'name': 'multibar probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
                   'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'multibar-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
        enabled = list(enabled) + ['multibar-probe']
    prof = os.path.join(out, 'profile.json')
    json.dump({'enabled': list(enabled), 'order': list(enabled)}, open(prof, 'w'))
    for p in ('mp.json', 'dsload.log', 'player.log', os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        aside(os.path.join(I, p), out)
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
    mark = len(dlog())
    log('load', os.path.basename(src), cmd({'cmd': 'load_save', 'file': dst}))
    t0 = time.time()
    while time.time() - t0 < 400:                     # save_backup question: Load
        if 'saves: warning shown' in dlog()[mark:]: time.sleep(1.5); request({'cmd': 'warn_load'}); break
        try:
            if status().get('scene') == 2: break
        except Exception: pass
        time.sleep(1)
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=2)
    cmd({'cmd': 'set_speed', 'speed': 0})
    time.sleep(3)
    return mark


def down():
    try:
        if r.alive(r.read_pid(X)): r.stop(X)
    except Exception as e: log('stop:', e)


def inject(ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return cmd(o)


SAME = ('tree', 'skills', 'equip', 'inv')
def same_units(a, b):
    """units whose skill trees, skills (with auto-use), items differ between two parsed saves"""
    return {g[:8]: [k for k in SAME if (b.get(g) or {}).get(k) != u[k]] for g, u in a.items() if [k for k in SAME if (b.get(g) or {}).get(k) != u[k]]}


def stock_form(after, live_exp):
    """the save's bars: 4 slots per unit = stock_bar of the live bar"""
    return {g[:8]: (after.get(g, {}).get('quick'), stock_bar(q)) for g, q in live_exp.items() if after.get(g, {}).get('quick') != stock_bar(q)}


def run_e1(out):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    L = dlog()
    check('E1: module on (15 hooks + AI order)', 'weapon skill slot on (16 hooks; AI auto-use order weapon skill first)' in L,
          [l for l in L.splitlines() if 'weapon-slot' in l][-6:])
    if not checks[-1]['ok']: raise SystemExit('module not on: stop')
    src = os.path.join(out, 'save_in', 'owner_5slot.json'); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(OWNER_SAVE, src)
    before = units_of(src)
    check('E1: the source save has 5 slots per unit (0.1.1)', all(len(u['quick']) == 5 for u in before.values()), {g[:8]: u['quick'] for g, u in before.items()})
    dm = load(src, out)
    L = dlog()[dm:]
    wl = [l for l in L.splitlines() if 'weapon skills (' in l]
    mods = set(wl[0].split(': ', 2)[-1].split()) if wl else set()
    check('E1: the module reads the 8 weapon skills from the game table (same as the wiki table)', mods == W, (sorted(mods), sorted(W)))
    exp = {g: expect_new(u) for g, u in before.items()}
    got = live(list(before))
    check('E1: live slots (units on the map) = the saved 5 slots', len(got) >= 2 and all(got[g] == exp[g] for g in got),
          {g[:8]: (before[g]['quick'], got.get(g), exp[g]) for g in got if got[g] != exp[g]} or len(got))
    check('E1: the bar has its weapon slot', 'skill bar: weapon slot added' in dlog(), [l for l in dlog().splitlines() if 'skill bar' in l][-3:])
    p = save_now('ws_e1')
    after = units_of(p) if p else {}
    out_bar = [g[:8] for g, q in exp.items() if q[4] and stock_bar(q).count(q[4]) == 0]
    check('E1: the save has 4 slots per unit, the weapon skill where the stock game keeps it (first empty free slot, or none)',
          after and set(after) == set(before) and not stock_form(after, exp), stock_form(after, exp) or len(after))
    check('E1: the test world has units with the weapon skill out of the bar (4 free slots full)', len(out_bar) >= 2, out_bar)
    check('E1: nothing else of the units changes in the save (skill trees, skills + auto-use choices, items)', after and not same_units(before, after),
          same_units(before, after))
    got2 = live(list(before))
    check('E1: after the save the live bars still have the weapon slot (no visible change)', got2 == got, {g[:8]: (got.get(g), got2.get(g)) for g in got if got2.get(g) != got.get(g)})
    log('E1 bars:', {g[:8]: (exp[g], after.get(g, {}).get('quick')) for g in exp})
    check('E1: no exception', not errors(mark), errors(mark)[:5])
    down()
    return p, exp


def run_e2(out, e1_save):
    mark = up(out, ['multiplayer'])
    check('E2: weapon-slot off (the game without the mod)', 'weapon skill slot on' not in dlog())
    src = os.path.join(out, 'save_in', 'ws_e1_stock.json'); shutil.copy2(e1_save, src)
    before = units_of(src)
    load(src, out)
    got = live(list(before))
    check('E2: the world loads with the saved 4-slot bars', len(got) >= 2 and all(got[g] == before[g]['quick'] for g in got),
          {g[:8]: (before[g]['quick'], got.get(g)) for g in got if got[g] != before[g]['quick']} or len(got))
    p = save_now('ws_e2')
    after = units_of(p) if p else {}
    check('E2: the game writes it back unchanged (4-slot bars, skill trees, skills, items)',
          after and all(after.get(g, {}).get('quick') == u['quick'] for g, u in before.items()) and not same_units(before, after),
          ({g[:8]: (u['quick'], after.get(g, {}).get('quick')) for g, u in before.items() if after.get(g, {}).get('quick') != u['quick']}, same_units(before, after)))
    check('E2: no exception', not errors(mark), errors(mark)[:5])
    down()
    return p


def run_e3(out, e2_save, e1_save, e1_live):
    mark = up(out, ['multiplayer', 'weapon-slot'], probe=True)
    src = os.path.join(out, 'save_in', 'ws_e2_stock.json'); shutil.copy2(e2_save, src)
    before = units_of(src); e1 = units_of(e1_save)
    load(src, out)
    got = live(list(before))
    check('E3: back on, the 4-slot save: live bars = those of E1 (weapon skill back in the weapon slot)',
          len(got) >= 2 and all(got[g] == e1_live[g] for g in got), {g[:8]: (before[g]['quick'], got.get(g), e1_live[g]) for g in got if got[g] != e1_live[g]} or len(got))
    p = save_now('ws_e3'); after = units_of(p) if p else {}
    check('E3: the save equals E1\'s (bars, skill trees, skills, items: a stable round trip)',
          after and all(after.get(g, {}).get('quick') == u['quick'] for g, u in e1.items()) and not same_units(e1, after),
          ({g[:8]: (u['quick'], after.get(g, {}).get('quick')) for g, u in e1.items() if after.get(g, {}).get('quick') != u['quick']}, same_units(e1, after)))
    # game events never change the weapon slot; bar slot 5 uses the weapon skill
    mp = status().get('map', 0)
    g = next((g for g in got if e1_live[g][4]), None)
    check('E3: a unit on the map has a weapon skill', g is not None, got)
    if g:
        q0 = e1_live[g]; w = q0[4]; free = next((i for i in range(4) if not q0[i]), None)
        cmd({'cmd': 'ord_select', 'guids': g, 'group': 3003}); time.sleep(1)
        inject('SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=w, i32_48=4); time.sleep(1)
        q1 = live([g])[g]
        check('E3: a Set to slot 4 (drop on the weapon slot) changes nothing', q1 == q0, (q1, q0))
        inject('SelectPlayerUnitRequested', i32_16=-1, guid_20=g); time.sleep(2)   # the bar shows this unit
        dm = len(dlog()); e0 = len(errors(mark))
        inject('UseQuickSlotSkillRequested', i32_16=5); time.sleep(2)
        adds = [l.split('AddUseSkill ', 1)[1].split() for l in dlog()[dm:].splitlines() if 'multibar-probe: AddUseSkill ' in l]
        check('E3: bar slot 5 makes the game use the weapon skill (AddUseSkill unit + weapon skill)', [g, w] in [a[:2] for a in adds], (g[:8], w, adds[:4]))
        check('E3: bar slot 5 throws nothing', len(errors(mark)) == e0, errors(mark)[e0:][:3])
        if free is not None:
            inject('SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=w, i32_48=free); time.sleep(1)
            q2 = live([g])[g]
            want = list(q0); want[free] = w            # the old mod's behaviour (the Set handler may also refuse an extra skill)
            check('E3: the weapon skill set into free slot %d stays in the weapon slot' % free, q2 in (want, q0), (q2, want))
    check('E3: no exception', not errors(mark), errors(mark)[:5])
    down()


def run_d(out):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    src = os.path.join(out, 'save_in', 'old_layout.json'); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(OLD_SAVE, src)
    before = units_of(src)
    load(src, out)
    got = live(list(before))
    exp = {g: expect_new(u, old_layout=True) for g, u in before.items()}
    p0 = save_now('ws_d0'); s0 = units_of(p0) if p0 else {}
    check('D: an old binary-mod save (weapon skill at 0) gets the new layout (live) and is saved in stock form',
          all(got[g] == exp[g] for g in got) and s0 and not stock_form(s0, exp),
          ({g[:8]: (before[g]['quick'], got.get(g), exp[g]) for g in got if got[g] != exp[g]}, stock_form(s0, exp)))
    check('D: the test unit is on the shown map', EQ_UNIT in got, list(got))
    check('D: no exception (first load)', not errors(mark), errors(mark)[:5])
    down()
    if not p0: return
    # the stock-form save of the same world, mod on: weapon slots back, equip, casts
    mark = up(out, ['multiplayer', 'weapon-slot'])
    src = os.path.join(out, 'save_in', 'ws_d0_stock.json'); shutil.copy2(p0, src)
    load(src, out)
    got = live(list(before))
    check('D: its stock-form save loads with the same weapon slots', len(got) >= 2 and all(got[g] == exp[g] for g in got),
          {g[:8]: (s0[g]['quick'], got.get(g), exp[g]) for g in got if got[g] != exp[g]})
    u0 = s0.get(EQ_UNIT, {})
    idx = (u0.get('inv') or []).index(EQ_ITEM) if EQ_ITEM in (u0.get('inv') or []) else -1
    check('D: the test unit has a WoodClub to equip', idx >= 0, u0.get('inv'))
    mp = status().get('map', 0)
    if idx >= 0:
        cmd({'cmd': 'ord_select', 'guids': EQ_UNIT, 'group': 3003}); time.sleep(1)
        inject('EquipItemRequested', i32_16=mp, guid_20=EQ_UNIT, i32_36=0, guid_40=EQ_UNIT, i32_56=idx); time.sleep(3)
        q = live([EQ_UNIT])[EQ_UNIT]
        want = list(exp[EQ_UNIT][:4]) + [EQ_SKILL]
        check('D: equip WoodClub: weapon slot = StunCharge, free slots unchanged', q == want, (exp[EQ_UNIT], q, want))
        p1 = save_now('ws_d1'); u1 = units_of(p1)[EQ_UNIT] if p1 else {}
        act = (u1.get('skills') or {}).get('ActiveSkills') or []
        check('D: the unit has StunCharge and no longer WhirlwindSlash (unequip removed it)', EQ_SKILL in act and OLD_SKILL not in act,
              (act, u1.get('equip')))
    check('D: no exception (stock-form load, equip)', not errors(mark), errors(mark)[:5])
    # each unit's weapon skill at the closest enemy (the order a target click gives), then the game runs. The stock game
    # throws in two skill routines with these orders in a hidden copy (control run without weapon-slot, 2026-10-25:
    # SKILL_StrangeSeed and SKILL_SummonBubbling <SkillRoutine> NullReferenceException): those two are not counted.
    m2 = len(rd(os.path.join(I, 'data', 'ErrorLog.txt')))
    us = cmd({'cmd': 'list_units'}).get('units') or []
    foes = [e for e in us if e.get('faction') == 3 and not e.get('dead') and 'cell' in e]
    mine = [u for u in us if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    ws = {u['guid']: (live([u['guid']]).get(u['guid']) or [None] * 5)[4] for u in mine}
    ok = 0
    for u in mine:
        if not ws.get(u['guid']) or not foes: continue
        f = min(foes, key=lambda e: math.dist(e['cell'], u['cell']))
        ok += 1 if inject('UseSkillTargetRequested', i32_16=mp, guid_20=u['guid'], guid_36=f['guid'], str_56=ws[u['guid']]).get('ok') else 0
    check('D: enemies on the map and weapon-skill orders accepted', foes and ok >= 2, (len(foes), ok, {g[:8]: w for g, w in ws.items()}))
    cast = {}
    cmd({'cmd': 'set_speed', 'speed': 1})
    for name in ('ws_d2', 'ws_d3'):
        time.sleep(12)
        cmd({'cmd': 'set_speed', 'speed': 0}); time.sleep(1)
        sv = units_of(save_now(name))
        cast = {g[:8]: (w, sv.get(g, {}).get('cooldowns', {}).get(w)) for g, w in ws.items() if w and (sv.get(g, {}).get('cooldowns', {}).get(w) or 0) > 0}
        if cast: break
        cmd({'cmd': 'set_speed', 'speed': 1})
    check('D: a unit cast its weapon-slot skill (it has a cooldown)', cast, ({g[:8]: w for g, w in ws.items()}, cast))
    blocks = rd(os.path.join(I, 'data', 'ErrorLog.txt'))[m2:].split('[Exception] ')[1:]
    stock = ('SKILL_StrangeSeed+<SkillRoutine>', 'SKILL_SummonBubbling+<SkillRoutine>')
    other = [b.strip()[:300] for b in blocks if not (b.startswith('NullReferenceException') and any(s in b.split('\n')[1] for s in stock if len(b.split('\n')) > 1))]
    check('D: no exception while the casts run (except the two stock skill-routine ones)', not other, other[:3])
    down()


def run_f(out):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    src = os.path.join(out, 'save_in', 'fill_fixture.json'); os.makedirs(os.path.dirname(src), exist_ok=True)
    want = make_fill_fixture(src)
    before = units_of(src)
    dm = len(dlog())
    load(src, out)
    L = [l for l in dlog()[dm:].splitlines() if 'load end' in l]
    log('fill:', L[-8:])
    got = live(list(before))
    after = units_of(save_now('ws_f'))
    exp = {g: expect_new(u) for g, u in before.items()}
    once = lambda lst, w: (lst or []).count(w) == 1
    check('F: every armed unit has its weapon skill in the weapon slot (live), free slots as saved; the save in stock form',
          len(got) >= 2 and all(got[g] == exp[g] for g in got) and all(exp[g][4] == w for g, w in want.items()) and not stock_form(after, exp),
          ({g[:8]: (before[g]['quick'], got.get(g), exp[g]) for g in before if got.get(g, exp[g]) != exp[g] or exp[g][4] != want.get(g, exp[g][4])}, stock_form(after, exp)))
    check('F: the weapon skill is once in the unit\'s skills and once in its extra skills (incl. the full-bar and no-extra units)',
          all(once((after[g]['skills'] or {}).get('ActiveSkills'), w) and once((after[g]['tree'] or {}).get('ExtraSkills'), w) for g, w in want.items()),
          {g[:8]: ((after[g]['skills'] or {}).get('ActiveSkills'), (after[g]['tree'] or {}).get('ExtraSkills')) for g in want})
    check('F: no exception', not errors(mark), errors(mark)[:5])
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
        steps = os.environ.get('WS_STEPS', 'e,d,f').split(',')
        if 'e' in steps:
            e1, e1_live = run_e1(OUT)
            e2 = run_e2(OUT, e1) if e1 else None
            if e2: run_e3(OUT, e2, e1, e1_live)
        if 'd' in steps: run_d(OUT)
        if 'f' in steps: run_f(OUT)
    finally:
        down()
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(checks, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(checks) - len(bad), len(checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
