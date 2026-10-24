#!/usr/bin/env python3
"""Windows VM run of the weapon-slot checks (test/weapon_slot_test.py phases A and D) through winrig (ds_multiplayer
win/winbot, WINBOT_DIR), the VM running visibly with the agent up. The mod-off phases (B, C) run the stock quick-slot
code, the same on both systems: Mac only.
  python3 weapon_slot_vm.py --mods <dir with weapon-slot/> [--out DIR]
weapon-slot goes into the profile's mods folder for the run; anything installed under that name is moved aside and
moved back after (never deleted). Exit 0 = every check passed."""
import argparse, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, T_DIR)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402
import weapon_slot_test as T  # noqa: E402

KEEP = r'C:\dsm\ToDelete'
WORK = r'C:\dsm\weapon_slot_vm'
check, log = T.check, T.log
BAD = T.BAD


def inject(w, ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return w.cmd(o)


def live(w, guids):
    out = {g: w.cmd(cmd='ord_read', guid=g).get('quick') for g in guids}
    return {g: q for g, q in out.items() if q is not None}


def errors(w):
    t = (w.text(W.PROF + r'\player.log') or '') + (w.text(W.DATA + r'\ErrorLog.txt') or '')
    return [l for l in t.splitlines() if any(b in l for b in BAD)]


def save_now(w, out, name):
    rp = W.DATA + '\\Saves\\' + name + '.json'
    w.cmd(cmd='ord_save', name=name)
    w.wait(lambda: w.exists(rp), 'save ' + name, 90)
    time.sleep(2)
    lp = os.path.join(out, name + '.json'); w.get(rp, lp)
    return lp


def load(w, local, name):
    g = WORK + '\\' + name + '.json'
    w.upload(local, g)
    log('load', w.cmd(cmd='load_save', file=w.expand(g)))
    w.wait(w.loaded, 'the world loaded', 400)
    w.cmd(cmd='set_speed', speed=0); time.sleep(3)


def phase_a(w, out):
    L = w.text(W.PROF + r'\dsload.log') or ''
    check('A: module on (16 hooks, AI order)', 'weapon skill slot on (16 hooks; AI auto-use order weapon skill first)' in L,
          [l for l in L.splitlines() if 'weapon-slot' in l][-6:])
    before = T.units_of(T.OWNER_SAVE)
    load(w, T.OWNER_SAVE, 'owner_4slot')
    L = w.text(W.PROF + r'\dsload.log') or ''
    wl = [l for l in L.splitlines() if 'weapon skills (' in l]
    check('A: the 8 weapon skills read from the game table', wl and set(wl[-1].split(': ', 2)[-1].split()) == T.W, wl[-1:])
    exp = {g: T.expect_new(u) for g, u in before.items()}
    got = live(w, list(before))
    check('A: live slots (units on the map) migrated', len(got) >= 2 and all(got[g] == exp[g] for g in got),
          {g[:8]: (got[g], exp[g]) for g in got if got[g] != exp[g]} or len(got))
    check('A: the bar has its weapon slot', 'skill bar: weapon slot added' in L, [l for l in L.splitlines() if 'skill bar' in l][-2:])
    after = T.units_of(save_now(w, out, 'ws_vm_a'))
    check('A: the save written: every unit migrated, 5 slots', {g: u['quick'] for g, u in after.items()} == exp,
          {g[:8]: (after.get(g, {}).get('quick'), exp[g]) for g in exp if after.get(g, {}).get('quick') != exp[g]})
    mp = w.cmd(cmd='status').get('map', 0)
    g = next((g for g in got if exp[g][4]), None)
    if g:
        e0 = len(errors(w))
        inject(w, 'SelectPlayerUnitRequested', i32_16=0, guid_20=g); time.sleep(2)
        w.gshot(os.path.join(out, 'win_bar_selected.png'))
        inject(w, 'SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=exp[g][4], i32_48=4); time.sleep(2)
        check('A: a Set to slot 4 changes nothing', live(w, [g]).get(g) == exp[g], live(w, [g]).get(g))
        inject(w, 'UseQuickSlotSkillRequested', i32_16=5); time.sleep(2)
        check('A: a click on bar slot 5 throws nothing', len(errors(w)) == e0, errors(w)[e0:][:3])
    check('A: no exception', not errors(w), errors(w)[:4])


def phase_d(w, out):
    before = T.units_of(T.OLD_SAVE)
    e0 = len(errors(w))
    load(w, T.OLD_SAVE, 'old_layout')
    exp = {g: T.expect_new(u, old_layout=True) for g, u in before.items()}
    got = live(w, list(before))
    s0 = T.units_of(save_now(w, out, 'ws_vm_d0'))
    check('D: an old binary-mod save gets the new layout (live + saved)',
          all(got[g] == exp[g] for g in got) and {g: u['quick'] for g, u in s0.items()} == exp,
          {g[:8]: (got.get(g), s0.get(g, {}).get('quick'), exp[g]) for g in before if s0.get(g, {}).get('quick') != exp[g]})
    u0 = s0.get(T.EQ_UNIT, {}); inv = u0.get('inv') or []
    if T.EQ_UNIT in got and T.EQ_ITEM in inv:
        mp = w.cmd(cmd='status').get('map', 0)
        w.cmd(cmd='ord_select', guids=T.EQ_UNIT, group=3003); time.sleep(1)
        inject(w, 'EquipItemRequested', i32_16=mp, guid_20=T.EQ_UNIT, i32_36=0, guid_40=T.EQ_UNIT, i32_56=inv.index(T.EQ_ITEM)); time.sleep(4)
        q = live(w, [T.EQ_UNIT]).get(T.EQ_UNIT)
        want = [None if s == T.EQ_SKILL else s for s in u0['quick'][:4]] + [T.EQ_SKILL]
        check('D: equip WoodClub: weapon slot = StunCharge, free slots unchanged', q == want, (u0['quick'], q, want))
        act = (T.units_of(save_now(w, out, 'ws_vm_d1')).get(T.EQ_UNIT, {}).get('skills') or {}).get('ActiveSkills') or []
        check('D: the unit has StunCharge and no longer WhirlwindSlash', T.EQ_SKILL in act and T.OLD_SKILL not in act, act)
    else:
        check('D: the WoodClub unit is on the shown map', False, (list(got), inv))
    check('D: no exception', len(errors(w)) == e0, errors(w)[e0:][:4])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mods', required=True); ap.add_argument('--out', default='weapon_slot_vm_out')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True); T.OUT = out
    w = W.Win(); w.up()
    keep = KEEP + r'\weapon_slot_vm_' + time.strftime('%Y%m%d-%H%M%S')
    md = W.ROOT + r'\mods\weapon-slot'
    w.kill_all()
    for f in ('weapon-slot.dll', 'weapon-slot.dylib', 'mod.json'):
        if w.exists(md + '\\' + f): w.move(md + '\\' + f, keep + '\\' + f)
    for f in ('weapon-slot.dll', 'mod.json'):
        w.upload(os.path.join(a.mods, 'weapon-slot', f), md + '\\' + f)
    try:
        try:
            w.prepare(lang=0, consent='no', active=['multiplayer', 'weapon-slot'], settings={})
            w.start(env={'DSL_CLOUD': '0'})
            phase_a(w, out)
            w.stop(); w.start(env={'DSL_CLOUD': '0'})     # a fresh game per world, as the Mac test does
            phase_d(w, out)
        except Exception as e:
            check('run finished', False, repr(e))
        finally:
            w.logs(out)
            w.stop()
    finally:
        w.kill_all()
        for f in ('weapon-slot.dll', 'mod.json'):
            if w.exists(md + '\\' + f): w.move(md + '\\' + f, keep + '\\test_' + f)
            if w.exists(keep + '\\' + f): w.move(keep + '\\' + f, md + '\\' + f)
        json.dump(T.checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in T.checks if not c['ok']]
    for c in T.checks: print(('PASS ' if c['ok'] else 'FAIL ') + c['name'])
    print(f'\nRESULT {len(T.checks) - len(fails)} PASS {len(fails)} FAIL; out {out}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
