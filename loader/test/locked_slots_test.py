#!/usr/bin/env python3
"""locked-slots module (Locked Slots): the right-click menu, the lock icon, Sort, the return to base and the save.
Hidden test copy (lrig.start: guard, cap, DSL_NOTCH=0, muted), one rig lease, one copy at a time. World = the wiki's
public base save (4 player units), copied under a NEW world guid with resource stacks put into the bags; nothing of
the owner is read or written.

  locked_slots_test.py run [out]       every step below; exit 1 on a failed check
  locked_slots_test.py cmd '<json>'    rig socket command to the running copy
  env LS_COPY=<letter> (default Z), LS_MODS=<dir> (take locked-slots from <dir>/locked-slots, the unzipped package)

Real breaks it catches:
 N  the game's own Ban / Allow Auto Repair orders (the same event with a real slot in Index) still change the item's
    auto-repair flag and never touch a lock (only the marked Index is the mod's).
 M  the stock right-click of a slot (UIInput ShowInteraction_Inventory) shows the row "Keep on return" (an EMPTY slot
    too); its click (InteractionOptionUI.OnSelect, the element's own call) locks the slot through the game's order;
    right-click again shows "Do not keep on return"; the lock icon shows on the locked slots only; the lock is in
    the mod's data file.
 S  Sort (the game's SortItemRequested order): the locked slots keep their items, the empty locked slot stays
    empty, the other items fill the first unlocked slots in the stock game's order (the same items sorted by the
    game in an unlocked unit), no item lost or doubled.
 R  return to base (the game's own map change, dungeon -> base): the resource stacks in locked slots stay in the
    bag; resources in unlocked slots leave it exactly as in the stock game (a unit with no lock loses them too);
    food stays (stock); the world total of each item key is unchanged (nothing lost or doubled).
 V  the save written with locks has the stock form: with the mod OFF the same world loads, and its save has
    exactly the same set of fields (JSON key paths) as the save written with the mod on; the kept items are
    ordinary items there."""
import copy, json, os, shutil, subprocess, sys, time, uuid
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
X = os.environ.get('LS_COPY', 'Z'); I = r.idir(X)
LB = os.path.dirname(HERE)
H = os.path.expanduser('~')
TPL = os.path.join(H, 'dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public wiki save (4 buffed units)
BAD = ('KeyNotFoundException', 'IndexOutOfRangeException', 'ArgumentOutOfRangeException', 'NullReferenceException',
       'InvalidCastException')
checks = []
OUT = None


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:900]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
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
def wait(what, f, timeout=60, every=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = f()
            if v: return v
        except Exception: pass
        time.sleep(every)
    log('timeout:', what); return None


# ------------------------------------------------------------------------------------------------ the test world
U = ['c88ac7e8-19f5-4532-914b-71c9929c4ba8', '1df4889e-7b6e-4db3-a3c9-bb02e1dd8e8a', 'ddeecda0-3650-4053-ba82-7974d87adae5']
U3 = '8b6e7144-afac-4eb7-a595-b5303c2bc2c3'           # 4th unit: a coif in bag slot 2 for the stock auto-repair options
LOCK = [0, 3, 5]                                  # U[0]'s locked slots: 0 and 3 hold resources, 5 is empty
RES = ('ITEM_LumaLog', 'ITEM_WildLongGrass')      # ItemType Resource (1): these leave the bag on a return


def make_fixture(dst):
    """the base save, new world guid; U0 bag: 0 LumaLog x3, 1 Hardtack (stock), 2 WildLongGrass x5, 3 LumaLog x2,
    6 WildLongGrass x2; U1 bag: 0 Hardtack, 1 WildLongGrass x4 (no lock: the stock behaviour); U2 bag: the same items
    as U0's unlocked ones in another order (the stock Sort order reference). Returns the world guid."""
    d = jload(TPL)
    w = str(uuid.uuid4())
    d['CampaignSaveHeader']['CampaignGuid'] = w; d['ClanSaveData']['CampaignGuid'] = w
    d['CampaignSaveHeader']['SaveName'] = 'LockedSlots__AutoSave_%s_0' % w.replace('-', '')
    css = d['CampaignSettingSaveData']['CampaignStartSettingSaveData']   # the template predates Lock Consecration:
    css.setdefault('LockConsecrationEnabled', False); css['LockConsecrationNoticeAcknowledged'] = True   # no Yes / No box at load
    B = d['EntityContainerSaveData']['ItemContainerSaveData']['Base']; E = d['EntityComponentSaveData']['EntityComponents']
    tmpl = {}
    for g, v in B['ItemSaveData'].items(): tmpl.setdefault(v['Key'], g)
    assert all(k in tmpl for k in RES + ('ITEM_ClothCoif',)), tmpl

    def mk(key, n, auto=False):
        g = str(uuid.uuid4())
        B['ItemSaveData'][g] = {'Guid': g, 'Key': key}; B['ItemAutoFixable'][g] = B['ItemAutoFixable'][tmpl[key]]
        E[g] = copy.deepcopy(E[tmpl[key]])
        return {'Guid': g, 'Key': key, 'Amount': n, 'AutoFixed': auto}

    def bag(u): return next(e['Data']['ItemSlots'] for e in E[u] if e['Type'] == 5)
    b0, b1, b2 = bag(U[0]), bag(U[1]), bag(U[2])
    hard = [s for s in b0 + b2 if s['Key'] == 'ITEM_Hardtack']
    empty = {'Guid': '00000000-0000-0000-0000-000000000000', 'Key': '', 'Amount': 0, 'AutoFixed': False}
    for b in (b0, b2):
        for i in range(len(b)): b[i] = dict(empty)
    b0[0] = mk('ITEM_LumaLog', 3); b0[1] = hard[0]; b0[2] = mk('ITEM_WildLongGrass', 5); b0[3] = mk('ITEM_LumaLog', 2)
    b0[6] = mk('ITEM_WildLongGrass', 2)
    b1[1] = mk('ITEM_WildLongGrass', 4)
    b2[4] = mk('ITEM_WildLongGrass', 2); b2[7] = hard[1]; b2[9] = mk('ITEM_WildLongGrass', 5)
    bag(U3)[2] = mk('ITEM_ClothCoif', 1, auto=True)   # the stock auto-repair options (unmarked orders) act on it
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    json.dump(d, open(dst, 'w'))
    return w


def bags(save):
    """unit guid -> [(key, amount) or None per slot]"""
    d = jload(save); E = d['EntityComponentSaveData']['EntityComponents']; out = {}
    for u in d['PlayerUnitsSaveData']['QuickSlots']:
        b = next((e['Data']['ItemSlots'] for e in E.get(u, []) if e['Type'] == 5), [])
        out[u] = [(s['Key'], s['Amount']) if s.get('Key') else None for s in b]
    return out


def autofix(save, u, slot):
    d = jload(save)
    return next(e['Data']['ItemSlots'][slot].get('AutoFixed') for e in d['EntityComponentSaveData']['EntityComponents'][u] if e['Type'] == 5)


def totals(save):
    """item key -> amount in every bag / storage slot of the world + every item on the ground (all maps)"""
    d = jload(save); t = {}
    for comps in d['EntityComponentSaveData']['EntityComponents'].values():
        for e in comps:
            for s in (e.get('Data') or {}).get('ItemSlots') or []:
                if s.get('Key'): t[s['Key']] = t.get(s['Key'], 0) + s['Amount']
    for m in d['EntityContainerSaveData']['ItemContainerSaveData'].values():
        for s in (m.get('OnMapItems') or {}).values():
            if s.get('Key'): t[s['Key']] = t.get(s['Key'], 0) + s['Amount']
    return t


def key_paths(o, p='', out=None):
    """every JSON key path, list items folded (the save's form, not its values)"""
    out = set() if out is None else out
    if isinstance(o, dict):
        for k, v in o.items():
            q = p + '/' + ('<guid>' if len(k) == 36 and k.count('-') == 4 else k); out.add(q); key_paths(v, q, out)
    elif isinstance(o, list):
        for v in o: key_paths(v, p + '[]', out)
    return out


# ------------------------------------------------------------------------------------------------ the copy
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


STOCK_RETRY = 'DSDungeonPreDataGenerator'           # the game's dungeon generator logs an NRE and retries with seed + 1
def errors(mark, text=None):
    """exception lines in player.log / ErrorLog since the start of this run (not the stock generator retry)"""
    t = (text if text is not None else rd(os.path.join(I, 'player.log')) + rd(os.path.join(I, 'data', 'ErrorLog.txt'))[mark:]).splitlines()
    return [l for i, l in enumerate(t) if any(b in l for b in BAD) and not any(STOCK_RETRY in x for x in t[i + 1:i + 3])]


def up(out, enabled):
    if r.alive(r.read_pid(X)): raise SystemExit('copy %s already running' % X)
    md = os.path.join(out, 'mods')
    if os.path.exists(md): aside(md, out)
    os.makedirs(md)
    for m in enabled:
        src = os.path.join(os.environ['LS_MODS'], m) if m == 'locked-slots' and os.environ.get('LS_MODS') else os.path.join(LB, 'build', 'mods', m)
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
    while time.time() - t0 < 400:                 # save_backup question: Load
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


def lstate(): return cmd({'cmd': 'lks_state'})
def locked(u): return next((x['slots'] for x in lstate().get('units', []) if x['unit'] == u), [])


def menu(u, slot):
    """the stock right-click of a bag slot; returns our row ({text, lock}) or None"""
    cmd({'cmd': 'lks_menu', 'unit': u, 'slot': slot})
    rows = wait('menu rows', lambda: cmd({'cmd': 'lks_rows'}).get('rows'), timeout=10)
    return next((x for x in rows or [] if x.get('ours')), None), rows


def on_map(m):                                    # on that map, no transition running
    s = status(); return s.get('map') == m and not s.get('map_transition') and not s.get('busy') and not (s.get('map_loading') or {}).get('is_loading')


# ------------------------------------------------------------------------------------------------ phase A: mod on
def run_a(out):
    mark = up(out, ['multiplayer', 'locked-slots'])
    check('A: module on (6 hooks)', 'locked slots on (6 hooks' in dlog(), [l for l in dlog().splitlines() if 'locked-slots' in l][-6:])
    fx = os.path.join(out, 'save_in', 'lks_fixture.json'); world = make_fixture(fx)
    load(fx, out)
    st = lstate(); check('A: the world is known', st.get('world') == world, st)

    # ---- M: the bag window, the menu, the order, the icon
    inject('SelectPlayerUnitRequested', guid_20=U[0]); time.sleep(1)   # the game's own selection (view updates)
    log('open', cmd({'cmd': 'lks_open'})); time.sleep(2)
    icons = wait('bag slots shown', lambda: [s for s in cmd({'cmd': 'lks_icons'}).get('slots', []) if s['unit'] == U[0]], timeout=10)
    check('M: the unit window shows its bag slots (36)', icons and len(icons) == 36, icons and len(icons))
    for slot in LOCK:
        row, rows = menu(U[0], slot)
        check('M: right-click slot %d shows "Keep on return"%s' % (slot, ' (empty slot)' if slot == 5 else ''),
              row and row['text'] == 'Keep on return' and row['lock'] == 1, rows)
        cmd({'cmd': 'lks_pick'})
        ok = wait('lock %d' % slot, lambda: slot in locked(U[0]), timeout=10)
        check('M: its click locks slot %d (game order)' % slot, ok, lstate())
    row, rows = menu(U[0], 0)
    check('M: right-click a locked slot shows "Do not keep on return"', row and row['text'] == 'Do not keep on return' and row['lock'] == 0, rows)
    cmd({'cmd': 'lks_pick'}); wait('unlock 0', lambda: 0 not in locked(U[0]), timeout=10)
    check('M: its click unlocks the slot', locked(U[0]) == [3, 5], lstate())
    menu(U[0], 0); cmd({'cmd': 'lks_pick'}); wait('lock 0 again', lambda: 0 in locked(U[0]), timeout=10)
    time.sleep(1)
    icons = {s['slot']: s['icon'] for s in cmd({'cmd': 'lks_icons'}).get('slots', []) if s['unit'] == U[0]}
    check('M: the lock icon shows on locked slots only', icons and all(icons.get(i) == (1 if i in LOCK else 0) for i in icons),
          sorted(i for i, v in icons.items() if v))
    menu(U[0], 3); time.sleep(1)                  # one picture for the owner: lock icons + the open menu row
    log('picture', cmd({'cmd': 'ui_capture', 'path': os.path.join(out, 'locked_slots_menu.png')}))
    lf = os.path.join(I, 'data', 'ModData', 'locked-slots', 'locks_%s.txt' % world)
    check('M: the locks are in the data file', ('%s 0 3 5' % U[0]) in rd(lf), rd(lf))

    # ---- N: the game's own auto-repair orders (same event, Index = a real slot) still reach the stock code
    t0 = lstate().get('toggles')
    inject('SelectPlayerUnitRequested', guid_20=U3); time.sleep(1)   # as in the game: the bag shown is the selected unit's
    a0 = autofix(save_now('lks_af0'), U3, 2)
    inject('BanAutoFixItemRequested', i32_16=0, i32_20=-1, guid_24=U3, i32_40=2, u8_44=1); time.sleep(2)
    a1 = autofix(save_now('lks_af1'), U3, 2)
    inject('AllowAutoFixItemRequested', i32_16=0, i32_20=-1, guid_24=U3, i32_40=2, u8_44=1); time.sleep(2)
    a2 = autofix(save_now('lks_af2'), U3, 2)
    st = lstate()
    check('N: unmarked Ban / Allow Auto Repair still work (stock), no lock change', (a0, a1, a2) == (True, False, True)
          and st.get('toggles') == t0 and not any(x['unit'] == U3 for x in st.get('units', [])), ((a0, a1, a2), st))

    # ---- S: Sort of the locked unit and of the reference unit (no lock)
    before = bags(save_now('lks_before_sort'))
    for u in (U[0], U[2]): inject('SortItemRequested', i32_16=0, guid_20=u)
    time.sleep(3)
    after = bags(save_now('lks_after_sort'))
    b0, a0, ref = before[U[0]], after[U[0]], after[U[2]]
    check('S: locked slots keep their items, the empty locked slot stays empty',
          [a0[i] for i in LOCK] == [b0[i] for i in LOCK] == [('ITEM_LumaLog', 3), ('ITEM_LumaLog', 2), None], (b0[:8], a0[:8]))
    free = [i for i in range(len(a0)) if i not in LOCK]
    got = [a0[i] for i in free if a0[i]]
    want = [x for x in ref if x]                 # the stock game's order of the same items (unit with no lock)
    check('S: the other items fill the first unlocked slots in the stock order', got == want and all(a0[i] for i in free[:len(got)]),
          (a0[:10], ref[:10]))
    check('S: no item lost or doubled by Sort', sorted(x for x in a0 if x) == sorted(x for x in b0 if x), (b0[:10], a0[:10]))

    # ---- R: dungeon and back (the game's own map change)
    t0 = totals(save_now('lks_before_trip'))
    log('to dungeon', cmd({'cmd': 'trn_changemap', 'from': 0, 'to': 1, 'region': 0, 'floor': 1, 'guids': ','.join(U[:2])}))
    cmd({'cmd': 'set_speed', 'speed': 1})
    check('R: the units reached the dungeon', wait('dungeon', lambda: on_map(1), timeout=240, every=2), status())
    cmd({'cmd': 'set_speed', 'speed': 0}); time.sleep(2)
    log('to base', cmd({'cmd': 'trn_changemap', 'from': 1, 'to': 0, 'flags': 3, 'guids': ','.join(U[:2])}))
    cmd({'cmd': 'set_speed', 'speed': 1})
    back = wait('base', lambda: on_map(0), timeout=240, every=2)
    cmd({'cmd': 'set_speed', 'speed': 0}); time.sleep(3)
    check('R: the units are back at base', back, status())
    sv = save_now('lks_after_trip'); b = bags(sv); t1 = totals(sv)
    u0, u1 = b[U[0]], b[U[1]]
    check('R: locked resource stacks stay in the bag', u0[0] == ('ITEM_LumaLog', 3) and u0[3] == ('ITEM_LumaLog', 2), u0[:10])
    check('R: resources in unlocked slots left the bag (stock)', not any(x and x[0] in RES for i, x in enumerate(u0) if i not in LOCK), u0[:10])
    check('R: a unit with no lock loses its resources as in the stock game', not any(x and x[0] in RES for x in u1), u1[:10])
    check('R: food stays in the bag (stock)', any(x and x[0] == 'ITEM_Hardtack' for x in u0) and any(x and x[0] == 'ITEM_Hardtack' for x in u1), (u0[:10], u1[:10]))
    check('R: nothing lost or doubled (world totals)', all(t0.get(k) == t1.get(k) for k in RES), {k: (t0.get(k), t1.get(k)) for k in RES})
    check('R: the module kept 2 stacks', lstate().get('kept') == 2, lstate())
    check('A: no exception', not errors(mark), errors(mark)[:5])
    down()
    return sv, world


# ------------------------------------------------------------------------------------------------ phase V: mod off
def run_v(out, a_save):
    mark = up(out, ['multiplayer'])
    load(a_save, out)
    check('V: the save with locks loads with the mod off', status().get('scene') == 2, status())
    sv = save_now('lks_modoff')
    pa, pb = key_paths(jload(a_save)), key_paths(jload(sv))
    check('V: the same save fields with the mod on and off', pa == pb, {'only_mod_on': sorted(pa - pb)[:8], 'only_mod_off': sorted(pb - pa)[:8]})
    b = bags(sv)[U[0]]
    check('V: the kept items are ordinary items with the mod off', b[0] == ('ITEM_LumaLog', 3) and b[3] == ('ITEM_LumaLog', 2), b[:6])
    check('V: no exception', not errors(mark), errors(mark)[:5])
    down()


def lease():
    lane = 'dslock-%d' % os.getpid(); t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip(): return lane, p.stdout.strip()
        time.sleep(20)
    raise SystemExit('no rig lease in 30 min')


def main():
    global OUT
    if len(sys.argv) > 1 and sys.argv[1] == 'cmd':
        print(json.dumps(cmd(json.loads(sys.argv[2])), indent=1)); return 0
    OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(LB, 'test_env', 'locked_slots'))
    os.makedirs(OUT, exist_ok=True)
    if r.owner_game_running(): log('note: the owner\'s game runs (one hidden copy, nice, background)')
    lane, pair = lease()
    try:
        a, _ = run_a(OUT)
        if a: run_v(OUT, a)
    finally:
        down()
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(checks, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(checks) - len(bad), len(checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
