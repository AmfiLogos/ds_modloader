#!/usr/bin/env python3
"""weapon-slot module: saves both ways (mod on / off), the 6-slot bar, equip, and the weapon slot that game events
cannot change. Hidden test copy X (lrig.start: guard, cap, DSL_NOTCH=0, a hidden window so the bar draws), one rig
lease, one copy at a time. The owner's saves and backups are only READ (copied into <out>/save_in first).

  weapon_slot_test.py run [out]       every step below; exit 1 on a failed check
  weapon_slot_test.py cmd '<json>'    rig socket command to the running copy
  env WS_MODS=<dir>                   take weapon-slot from <dir>/weapon-slot (the unzipped release package)

Real breaks it catches (data loss, an exception that stops the bar, a slot that the game or the player changes):
 A  mod on, a 4-slot save (the owner's world, played without the mod): every unit gets 5 slots; the weapon skill
    (the unit's extra skill that is a weapon's skill) leaves its free slot for the weapon slot, every other skill keeps
    its place; the save written then has the same 5 slots; the bar has its 6th slot and no exception was thrown.
    A Set to index 4 (a drop on the weapon slot) changes nothing; setting the weapon skill into a free slot keeps it in
    the weapon slot too (the Set handler's duplicate removal); a click on bar slot 5 throws nothing.
 D  mod on, a save of the old binary mod (weapon skill at index 0, 2026-10-11 backup): turned into the new layout;
    equip a WoodClub over a greatsword: the weapon slot holds StunCharge, the free slots do not change, the unit has
    StunCharge and no longer WhirlwindSlash (the unequip still removes the old weapon's skill).
 B  mod off (loader + multiplayer only), the 5-slot save of A: the world loads, units keep the first 4 slots, and
    nothing else of the units changes in the save it writes (skill trees, extra skills, skills, items).
 C  mod on again, the 4-slot save of B: the world loads with 5 slots and no exception."""
import json, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('WS_COPY', 'X'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
OWNER_SAVE = os.path.join(H, 'Library/Application Support/CanOpener/Dungeon Settlers/Saves',
                          'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')   # 4-slot (read only)
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


def weapon_skills():                              # Equipment_ItemDefaultSkills[1] of every item (wiki table, same build)
    d = jload(ITEMS); rows = d if isinstance(d, list) else d.get('rows', d)
    rows = list(rows.values()) if isinstance(rows, dict) else rows
    return {s[1] for x in rows if isinstance(x, dict) for s in [x.get('Equipment_ItemDefaultSkills')] if isinstance(s, list) and len(s) > 1 and s[1] != 'NULL'}
W = weapon_skills()


def units_of(save):
    """per unit: quick slots, extra skills, skill component, equipment + inventory keys"""
    d = jload(save); P = d['PlayerUnitsSaveData']; E = d['EntityComponentSaveData']['EntityComponents']
    out = {}
    for g, q in P['QuickSlots'].items():
        c = {e['Type']: e['Data'] for e in E.get(g, [])}
        out[g] = {'quick': q['QuickSlots'], 'tree': P['SkillTrees'].get(g),
                  'skills': {k: c.get(8, {}).get(k) for k in ('BasicSkills', 'ActiveSkills', 'PassiveSkills', 'AutoUse')},
                  'equip': [s.get('Key') for s in c.get(6, {}).get('ItemSlots', [])], 'inv': [s.get('Key') for s in c.get(5, {}).get('ItemSlots', [])]}
    return out


def expect_new(u, old_layout=False):
    """the module's migration rule on one unit of a save file"""
    q = list(u['quick']) + [None] * (5 - len(u['quick']))
    w = next((s for s in (u['tree'] or {}).get('ExtraSkills') or [] if s in W), None)
    if not old_layout:
        if w and w in q[:4]: q = [None if s == w else s for s in q[:4]] + [w]
        return q
    if w and q[0] == w and q[4] != w: q = q[1:5] + [w]
    return q


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
def up(out, enabled):
    if r.alive(r.read_pid(X)): raise SystemExit('copy %s already running' % X)
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    for m in enabled:                             # WS_MODS = a folder with the shipped weapon-slot (unzipped package)
        src = os.path.join(os.environ['WS_MODS'], m) if m == 'weapon-slot' and os.environ.get('WS_MODS') else os.path.join(LB, 'build', 'mods', m)
        shutil.copytree(src, os.path.join(md, m))
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


def run_a(out):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    L = dlog()
    check('A: module on (14 hooks + AI order)', 'weapon skill slot on (15 hooks; AI auto-use order weapon skill first)' in L,
          [l for l in L.splitlines() if 'weapon-slot' in l][-6:])
    if not checks[-1]['ok']: raise SystemExit('module not on: stop')
    src = os.path.join(out, 'save_in', 'owner_4slot.json'); os.makedirs(os.path.dirname(src), exist_ok=True); shutil.copy2(OWNER_SAVE, src)
    before = units_of(src)
    check('A: the source save has 4 slots per unit', all(len(u['quick']) == 4 for u in before.values()), {g[:8]: u['quick'] for g, u in before.items()})
    dm = load(src, out)
    L = dlog()[dm:]
    wl = [l for l in L.splitlines() if 'weapon skills (' in l]
    mods = set(wl[0].split(': ', 2)[-1].split()) if wl else set()
    check('A: the module reads the 8 weapon skills from the game table (same as the wiki table)', mods == W, (sorted(mods), sorted(W)))
    got = live(list(before))
    exp = {g: expect_new(u) for g, u in before.items()}
    check('A: live slots (units on the map) = 4-slot save migrated (weapon skill to slot 4, other skills in place)',
          len(got) >= 2 and all(got[g] == exp[g] for g in got), {g[:8]: (before[g]['quick'], got.get(g), exp[g]) for g in got if got[g] != exp[g]} or len(got))
    moved = sum(1 for g in before if exp[g][4])
    log('migrated units:', {g[:8]: (before[g]['quick'], got.get(g)) for g in before})
    check('A: the bar has its weapon slot', 'skill bar: weapon slot added' in dlog(), [l for l in dlog().splitlines() if 'skill bar' in l][-3:])
    p = save_now('ws_a')
    after = units_of(p) if p else {}
    check('A: the save written: every unit migrated, 5 slots', {g: u['quick'] for g, u in after.items()} == exp,
          {g[:8]: (after.get(g, {}).get('quick'), exp[g]) for g in exp if after.get(g, {}).get('quick') != exp[g]})
    # game events never change the weapon slot
    st = status(); mp = st.get('map', 0)
    g = next((g for g in got if exp[g][4]), None)
    if g:
        w = exp[g][4]; free = next((i for i in range(4) if not exp[g][i]), None)
        cmd({'cmd': 'ord_select', 'guids': g, 'group': 3003}); time.sleep(1)
        inject('SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=w, i32_48=4); time.sleep(1)
        q1 = live([g])[g]
        check('A: a Set to slot 4 (drop on the weapon slot) changes nothing', q1 == exp[g], (q1, exp[g]))
        if free is not None:
            inject('SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=w, i32_48=free); time.sleep(1)
            q2 = live([g])[g]
            want = list(exp[g]); want[free] = w            # the old mod's behaviour (the Set handler may also refuse an extra skill)
            check('A: the weapon skill set into free slot %d stays in the weapon slot' % free, q2 in (want, exp[g]), (q2, want))
        inject('SelectPlayerUnitRequested', i32_16=-1, guid_20=g); time.sleep(2)   # the bar shows this unit
        sp = os.path.join(out, 'bar_selected.png'); aside(sp, out)
        cmd({'cmd': 'ui_teams', 'action': 'shot', 'path': sp}); time.sleep(2)
        e0 = len(errors(mark))
        inject('UseQuickSlotSkillRequested', i32_16=5); time.sleep(2)
        check('A: a click on bar slot 5 (UseQuickSlotSkill Index 5) throws nothing', len(errors(mark)) == e0, errors(mark)[e0:][:3])
        sp = os.path.join(out, 'bar_click5.png'); aside(sp, out)
        cmd({'cmd': 'ui_teams', 'action': 'shot', 'path': sp}); time.sleep(2)
        log('shot', sp, os.path.exists(sp))
    check('A: no exception', not errors(mark), errors(mark)[:5])
    down()
    return p, moved


def run_d(out):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    src = os.path.join(out, 'save_in', 'old_layout.json'); shutil.copy2(OLD_SAVE, src)
    before = units_of(src)
    load(src, out)
    got = live(list(before))
    exp = {g: expect_new(u, old_layout=True) for g, u in before.items()}
    p0 = save_now('ws_d0'); s0 = units_of(p0) if p0 else {}
    check('D: an old binary-mod save (weapon skill at 0) gets the new layout (live + saved)',
          all(got[g] == exp[g] for g in got) and {g: u['quick'] for g, u in s0.items()} == exp,
          {g[:8]: (before[g]['quick'], got.get(g), s0.get(g, {}).get('quick'), exp[g]) for g in before if s0.get(g, {}).get('quick') != exp[g] or got.get(g, exp[g]) != exp[g]})
    check('D: the test unit is on the shown map', EQ_UNIT in got, list(got))
    u0 = s0.get(EQ_UNIT, {})
    idx = (u0.get('inv') or []).index(EQ_ITEM) if EQ_ITEM in (u0.get('inv') or []) else -1
    check('D: the test unit has a WoodClub to equip', idx >= 0, u0.get('inv'))
    if idx >= 0:
        mp = status().get('map', 0)
        cmd({'cmd': 'ord_select', 'guids': EQ_UNIT, 'group': 3003}); time.sleep(1)
        inject('EquipItemRequested', i32_16=mp, guid_20=EQ_UNIT, i32_36=0, guid_40=EQ_UNIT, i32_56=idx); time.sleep(3)
        q = live([EQ_UNIT])[EQ_UNIT]
        want = [None if s == EQ_SKILL else s for s in u0['quick'][:4]] + [EQ_SKILL]
        check('D: equip WoodClub: weapon slot = StunCharge, free slots unchanged', q == want, (u0['quick'], q, want))
        p1 = save_now('ws_d1'); u1 = units_of(p1)[EQ_UNIT] if p1 else {}
        act = (u1.get('skills') or {}).get('ActiveSkills') or []
        check('D: the unit has StunCharge and no longer WhirlwindSlash (unequip removed it)', EQ_SKILL in act and OLD_SKILL not in act,
              (act, u1.get('equip')))
    check('D: no exception', not errors(mark), errors(mark)[:5])
    down()


SAME = ('tree', 'skills', 'equip', 'inv')
def run_b(out, a_save):
    mark = up(out, ['multiplayer'])
    check('B: module off', 'weapon skill slot on' not in dlog())
    src = os.path.join(out, 'save_in', 'ws_a_5slot.json'); shutil.copy2(a_save, src)
    before = units_of(src)
    load(src, out)
    got = live(list(before))
    p = save_now('ws_b')
    after = units_of(p) if p else {}
    check('B: mod off, 5-slot save: the world loads, units keep the first 4 slots (live + saved)',
          len(got) >= 2 and all(got[g] == before[g]['quick'][:4] for g in got) and all(after.get(g, {}).get('quick') == u['quick'][:4] for g, u in before.items()),
          {g[:8]: (before[g]['quick'], got.get(g), after.get(g, {}).get('quick')) for g in before if after.get(g, {}).get('quick') != before[g]['quick'][:4] or got.get(g, before[g]['quick'][:4]) != before[g]['quick'][:4]})
    diff = {g[:8]: [k for k in SAME if after.get(g, {}).get(k) != u[k]] for g, u in before.items()}
    check('B: nothing but the bar changes (skill trees, extra skills, skills, items equal)', not any(diff.values()),
          {g: (k, {kk: (before[gg][kk], after.get(gg, {}).get(kk)) for kk in k}) for gg in before for g, k in [(gg[:8], diff[gg[:8]])] if k})
    check('B: no exception', not errors(mark), errors(mark)[:5])
    down()
    return p


def run_c(out, b_save):
    mark = up(out, ['multiplayer', 'weapon-slot'])
    src = os.path.join(out, 'save_in', 'ws_b_4slot.json'); shutil.copy2(b_save, src)
    before = units_of(src)
    load(src, out)
    got = live(list(before))
    exp = {g: expect_new(u) for g, u in before.items()}
    p = save_now('ws_c'); after = units_of(p) if p else {}
    check('C: back on, the 4-slot save of B: every unit has 5 slots, free slots as saved',
          len(got) >= 2 and all(len(got[g]) == 5 and got[g][:4] == exp[g][:4] for g in got)
          and all(len(after.get(g, {}).get('quick') or []) == 5 and after[g]['quick'][:4] == exp[g][:4] for g in before),
          {g[:8]: (before[g]['quick'], got.get(g), after.get(g, {}).get('quick'), exp[g]) for g in before})
    log('C saved slots:', {g[:8]: after.get(g, {}).get('quick') for g in before})
    check('C: no exception', not errors(mark), errors(mark)[:5])
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
        a, moved = run_a(OUT)
        log('units whose weapon skill moved:', moved)
        run_d(OUT)
        if a:
            b = run_b(OUT, a)
            if b: run_c(OUT, b)
    finally:
        down()
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(checks, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(checks) - len(bad), len(checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
