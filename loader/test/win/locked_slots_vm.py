#!/usr/bin/env python3
"""Windows VM run of the Locked Slots checks (test/locked_slots_test.py on the Mac: parts M and R) through winrig
(ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up and the kit under test installed
(locked-slots in it; this run turns it on in the test profile only).
  python3 locked_slots_vm.py [--out DIR]
World = the wiki's public base save under a NEW world guid with resource stacks in the bags (the Mac test's fixture);
nothing of a player is read or written. Real breaks it catches on Windows:
 M  the stock right-click of a bag slot shows the row "Keep on return" (an EMPTY slot too); its click locks the slot
    through the game's order; right-click again shows "Do not keep on return"; the lock icon shows on the locked slots
    only; the locks are in the mod's data file.
 R  return to base (the game's own map change, dungeon -> base): resource stacks in locked slots stay in the bag,
    resources in unlocked slots leave it as in the stock game (a unit with no lock too), food stays, world totals of
    the resources unchanged.
Sort and the mod-off save form (parts S, V) run the same portable code: Mac only. Exit 0 = every check passed."""
import argparse, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, T_DIR)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402
import locked_slots_test as T  # noqa: E402  (fixture, bag / total readers)

WORK = r'C:\dsm\locked_slots_vm'
U, LOCK, RES = T.U, T.LOCK, T.RES
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def wait(w, what, f, timeout=60, every=0.5):
    try:
        return w.wait(f, what, timeout, every)
    except W.GameError:
        log('timeout:', what); return None


def errors(w):
    t = ((w.text(W.PROF + r'\player.log') or '') + (w.text(W.DATA + r'\ErrorLog.txt') or '')).splitlines()
    return [l for i, l in enumerate(t) if any(b in l for b in T.BAD) and not any(T.STOCK_RETRY in x for x in t[i + 1:i + 3])]


def load(w, local, name):
    g = WORK + '\\' + name + '.json'
    w.upload(local, g)
    mark = w.mark('dsload.log')
    log('load', w.cmd(cmd='load_save', file=w.expand(g)))
    t0 = time.time()
    while time.time() - t0 < 400:                                    # a save warning (none expected): answered Load
        new = w.read(W.PROF + r'\dsload.log', offset=mark)[0].decode('utf-8', 'replace')
        if 'saves: warning shown' in new:
            time.sleep(1.5); w.write(W.PROF + r'\softrestart\reg_request.json', json.dumps({'cmd': 'warn_load'})); break
        if (w.status() or {}).get('scene') == 2: break
        time.sleep(1)
    w.wait(lambda: (lambda s: s if s and s.get('tick', -1) > 0 else None)(w.loaded()), 'the world loaded', 400, 2)
    w.cmd(cmd='set_speed', speed=0); time.sleep(3)


def save_now(w, out, name):
    rp = W.DATA + '\\Saves\\' + name + '.json'
    w.cmd(cmd='ord_save', name=name)
    w.wait(lambda: (w.a('stat', path=rp).get('size') or 0) > 1000, 'save ' + name, 90)
    time.sleep(2)
    lp = os.path.join(out, 'saves', name + '.json'); w.get(rp, lp)
    return lp


def inject(w, ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return w.cmd(o)


def run(w, out):
    L = w.text(W.PROF + r'\dsload.log') or ''
    check('A: module on (6 hooks)', 'locked slots on (6 hooks' in L, [l for l in L.splitlines() if 'locked-slots' in l][-6:])
    fx = os.path.join(out, 'save_in', 'lks_fixture.json'); world = T.make_fixture(fx)
    load(w, fx, 'lks_fixture')
    lstate = lambda: w.cmd(cmd='lks_state')
    locked = lambda u: next((x['slots'] for x in lstate().get('units', []) if x['unit'] == u), [])
    st = lstate(); check('A: the world is known', st.get('world') == world, st)

    def menu(u, slot):
        w.cmd(cmd='lks_menu', unit=u, slot=slot)
        rows = wait(w, 'menu rows', lambda: w.cmd(cmd='lks_rows').get('rows'), 10)
        return next((x for x in rows or [] if x.get('ours')), None), rows

    # ---- M: the bag window, the menu, the order, the icon
    inject(w, 'SelectPlayerUnitRequested', guid_20=U[0]); time.sleep(1)
    log('open', w.cmd(cmd='lks_open')); time.sleep(2)
    icons = wait(w, 'bag slots shown', lambda: [s for s in w.cmd(cmd='lks_icons').get('slots', []) if s['unit'] == U[0]], 10)
    check('M: the unit window shows its bag slots (36)', icons and len(icons) == 36, icons and len(icons))
    for slot in LOCK:
        row, rows = menu(U[0], slot)
        check('M: right-click slot %d shows "Keep on return"%s' % (slot, ' (empty slot)' if slot == 5 else ''),
              row and row['text'] == 'Keep on return' and row['lock'] == 1, rows)
        w.cmd(cmd='lks_pick')
        check('M: its click locks slot %d (game order)' % slot, wait(w, 'lock %d' % slot, lambda: slot in locked(U[0]), 10), lstate())
    row, rows = menu(U[0], 0)
    check('M: right-click a locked slot shows "Do not keep on return"', row and row['text'] == 'Do not keep on return' and row['lock'] == 0, rows)
    w.cmd(cmd='lks_pick'); wait(w, 'unlock 0', lambda: 0 not in locked(U[0]), 10)
    check('M: its click unlocks the slot', locked(U[0]) == [3, 5], lstate())
    menu(U[0], 0); w.cmd(cmd='lks_pick'); wait(w, 'lock 0 again', lambda: 0 in locked(U[0]), 10)
    time.sleep(1)
    icons = {s['slot']: s['icon'] for s in w.cmd(cmd='lks_icons').get('slots', []) if s['unit'] == U[0]}
    check('M: the lock icon shows on locked slots only', icons and all(icons.get(i) == (1 if i in LOCK else 0) for i in icons),
          sorted(i for i, v in icons.items() if v))
    menu(U[0], 3); time.sleep(1)                                     # one picture: lock icons + the open menu row
    try: w.gshot(os.path.join(out, 'locked_slots_menu_win.png'), 'lks_menu')
    except W.GameError as e: log('picture:', e)
    lf = w.text(W.DATA + r'\ModData\locked-slots\locks_%s.txt' % world) or ''
    check('M: the locks are in the data file', ('%s 0 3 5' % U[0]) in lf, lf)

    # ---- R: dungeon and back (the game's own map change)
    t0 = T.totals(save_now(w, out, 'lks_before_trip'))
    on_map = lambda m: (lambda s: s.get('map') == m and not s.get('map_transition') and not s.get('busy')
                        and not (s.get('map_loading') or {}).get('is_loading'))(w.status())
    log('to dungeon', w.cmd(cmd='trn_changemap', **{'from': 0, 'to': 1, 'region': 0, 'floor': 1, 'guids': ','.join(U[:2])}))
    w.cmd(cmd='set_speed', speed=1)
    check('R: the units reached the dungeon', wait(w, 'dungeon', lambda: on_map(1), 240, 2), w.status())
    w.cmd(cmd='set_speed', speed=0); time.sleep(2)
    log('to base', w.cmd(cmd='trn_changemap', **{'from': 1, 'to': 0, 'flags': 3, 'guids': ','.join(U[:2])}))
    w.cmd(cmd='set_speed', speed=1)
    back = wait(w, 'base', lambda: on_map(0), 240, 2)
    w.cmd(cmd='set_speed', speed=0); time.sleep(3)
    check('R: the units are back at base', back, w.status())
    sv = save_now(w, out, 'lks_after_trip'); b = T.bags(sv); t1 = T.totals(sv)
    u0, u1 = b[U[0]], b[U[1]]
    check('R: locked resource stacks stay in the bag', u0[0] == ('ITEM_LumaLog', 3) and u0[3] == ('ITEM_LumaLog', 2), u0[:10])
    check('R: resources in unlocked slots left the bag (stock)', not any(x and x[0] in RES for i, x in enumerate(u0) if i not in LOCK), u0[:10])
    check('R: a unit with no lock loses its resources as in the stock game', not any(x and x[0] in RES for x in u1), u1[:10])
    check('R: food stays in the bag (stock)', any(x and x[0] == 'ITEM_Hardtack' for x in u0) and any(x and x[0] == 'ITEM_Hardtack' for x in u1), (u0[:10], u1[:10]))
    check('R: nothing lost or doubled (world totals)', all(t0.get(k) == t1.get(k) for k in RES), {k: (t0.get(k), t1.get(k)) for k in RES})
    check('R: the module kept 2 stacks', lstate().get('kept') == 2, lstate())
    check('A: no exception', not errors(w), errors(w)[:5])


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='locked_slots_vm_out'); a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    w = W.Win(); w.up(); w.kill_all()
    try:
        w.prepare(lang=0, consent='no', active=['multiplayer', 'locked-slots'], settings={})
        w.start(env={'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1'})
        run(w, out)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
