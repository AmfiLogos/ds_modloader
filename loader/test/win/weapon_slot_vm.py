#!/usr/bin/env python3
"""Windows VM run of the weapon-slot 0.2 checks (client only; test/weapon_slot_test.py) through winrig (ds_multiplayer
win/winbot, WINBOT_DIR), the VM running visibly with the agent up. World: weapon_slot_test's 4-slot fixture. Checks: the
module runs (7 hooks found by name on Windows), the save is the game's own after a load + save, the weapon button shows
and uses each unit's weapon skill (module log lines: Windows has no probe), a weapon swap moves it, no exception.
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


def shown_used(w, g, mp):
    """select g (portrait click): the module's last 'shows' line for g, then bar slot 5: its 'used' line for g"""
    pos = len(w.text(W.PROF + r'\dsload.log') or '')
    inject(w, 'SelectPlayerUnitRequested', i32_16=-1, guid_20=g); time.sleep(2)
    inject(w, 'UseQuickSlotSkillRequested', i32_16=5); time.sleep(2)
    inject(w, 'BaseControlModeRequested')
    L = (w.text(W.PROF + r'\dsload.log') or '')[pos:]
    used = [l.split(' -> ', 1)[1].strip() for l in L.splitlines() if 'weapon slot used: unit ' + g[:8] in l]
    return used[-1] if used else None


def phase(w, out):
    L = w.text(W.PROF + r'\dsload.log') or ''
    check('module on: client only, 7 hooks', 'weapon skill slot on (client only: 7 hooks, no game data written)' in L,
          [l for l in L.splitlines() if 'weapon-slot' in l][-6:])
    src = os.path.join(out, 'ws_world.json'); T.make_fill_fixture(src, free_eq=True)
    before = T.units_of(src)
    load(w, src, 'ws_world')
    mp = w.cmd(cmd='status').get('map', 0)
    on_map = list(live(w, list(before)))
    exp = {g: T.weapon_skill(before[g]) for g in on_map}
    for g, x in exp.items():
        got = shown_used(w, g, mp)
        check('unit %s: the weapon button uses %s' % (g[:8], x or 'nothing'), got == (x or '(none: no weapon skill)'), got)
    after = T.units_of(save_now(w, out, 'ws_vm_round'))
    diff = {g[:8]: [k for k in ('quick', 'tree', 'skills', 'equip', 'inv') if after.get(g, {}).get(k) != u[k]] for g, u in before.items()}
    diff = {g: d for g, d in diff.items() if d}
    check('the save is the game\'s own (quick slots, skill trees, skills + auto-use, items unchanged)', after and not diff, diff or len(after))
    inv = before[T.EQ_UNIT]['inv']
    if T.EQ_UNIT in exp and T.EQ_ITEM in inv:
        w.cmd(cmd='ord_select', guids=T.EQ_UNIT, group=3003); time.sleep(1)
        inject(w, 'EquipItemRequested', i32_16=mp, guid_20=T.EQ_UNIT, i32_36=0, guid_40=T.EQ_UNIT, i32_56=inv.index(T.EQ_ITEM)); time.sleep(4)
        q = live(w, [T.EQ_UNIT]).get(T.EQ_UNIT)
        check('swap: the game puts StunCharge in its first empty free slot (stock, 4 slots)', q and len(q) == 4 and q[3] == T.EQ_SKILL, q)
        check('swap: the weapon button uses StunCharge', shown_used(w, T.EQ_UNIT, mp) == T.EQ_SKILL)
        w.gshot(os.path.join(out, 'win_bar_weapon_slot.png'))
    else:
        check('swap unit on the shown map with WoodClub in its bag', False, (list(exp), inv))
    check('no exception', not errors(w), errors(w)[:4])


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
            phase(w, out)
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
