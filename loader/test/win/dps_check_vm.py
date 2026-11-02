#!/usr/bin/env python3
"""Windows VM run of Units Laboratory > Test Fights > DPS Check (custom-modes d6cf332: the DPS Check start move hooks
ControlModeProcessor.SwitchMode, not DefaultMode.ActivateMode, which on Windows is a 'ret 0' stub shared by 8695 methods),
through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up and the kit under test
installed. Driver: the test module units-probe (units_probe/, as units_lab_vm.py / ulab_new_vm.py).
  python3 dps_check_vm.py [--out DIR] [--probe DIR with units-probe/] [--secs 60]
Real breaks it catches on Windows:
  - the DPS Check world does not load, or the game crashes / hangs while the squad fights the scarecrow;
  - the start move hook is refused (or not made) on Windows, or it never finds the map's orders (the squad is not sent
    next to the scarecrow);
  - a custom-modes hook lands on a shared stub (the loader refuses it: any "refused" line in dsload.log);
  - the DPS meter counts nothing (no unit row with hits > 0).
Exit 0 = every check passed."""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import units_lab_vm as U  # noqa: E402  (Drv, make_library, check; imports winrig)
W = U.W
WORK = r'C:\dsm\dps_check_vm'
DRV = WORK + r'\drv'
U.DRV = DRV                                                          # U.Drv writes its requests to U.DRV
LIB = WORK + r'\lib\library.json'
WIKI = WORK + r'\wiki'
check, log, checks = U.check, U.log, U.checks
SITE = os.path.dirname(os.path.expanduser(os.environ.get('DSB_DIR', '~/dungeonsettlersdb/site/builder')))
OUT = ['.']
TO_DEL = W.WORK + r'\to_delete\dps_check_vm_' + time.strftime('%Y%m%d-%H%M%S')


def call(d, fn, s):
    r = d('call', fn=fn, s=s)
    try: return json.loads(r.get('out') or '{}')
    except ValueError: return {'raw': (r.get('out') or '')[:300]}


def tf_state(d): return ((d('state').get('cm') or {}).get('fights') or {}).get('tf') or {}


def attack(w, d):
    """the player's attack order, as dps_test.py order_attack on the Mac (the squad does not hit the scarecrow without one):
    a left click on each squad unit, a right click on the scarecrow. Game screen points (Unity: client pixels, y up) ->
    desktop pixels through the game window (client 1728x1000, winrig's windowed prefs; frame borders split evenly)."""
    try: w.wait(lambda: 'tf: DPS Check: start move (' in (w.text(W.PROF + r'\dsload.log') or ''), 'the start move', 120, 2)   # 0.1.49 line
    except Exception as e: log('start move wait:', e)
    time.sleep(12)                                                   # the squad walks next to the scarecrow
    win = next((x for p in w.procs() if p['pid'] == w.pid or not w.pid for x in p.get('windows', []) if x.get('w', 0) > 600), None)
    if not win:
        log('attack: no game window'); return
    bx = (win['w'] - 1728) / 2.0; left, bottom = win['x'] + bx, win['y'] + win['h'] - bx
    to = lambda s: (int(left + s[0]), int(bottom - s[1]))
    ch = win['h'] - 2 * bx - 30                                      # client height (title bar ~30 px)

    def look():
        p = call(d, 'tf_dps_test', 'pos').get('b', [])
        return next((b for b in p if b.get('pk') == 'BUILDING_DungeonScareCrow'), None), [b for b in p if b.get('u') == 1 and b.get('on')]
    sc, us = look()
    log('attack: window', win, 'scarecrow', sc and sc.get('s'), 'units', [u.get('s') for u in us])
    if not sc: return
    # the VM's small window can start with the scarecrow off screen (Mac copies show it): pan the game camera with the
    # arrow keys (then W/S) until it is inside the client area; the game window gets the keyboard by a click on its centre
    w.a('click', x=int(left + 864), y=int(bottom - ch / 2)); time.sleep(0.5)
    # centre the camera on the squad (it stands next to the scarecrow): a double click on the first squad portrait in the
    # top bar (seen on the VM 2026-11-02 at client ~(1168, 90) in a 1728-wide window); then the unit is selected
    if not 150 < sc['s'][1] < ch - 150:
        w.a('click', x=int(left + 1168), y=int(win['y'] + 121), double=True); time.sleep(2.0)
        sc2, us2 = look()
        if sc2: sc, us = sc2, us2
        log('attack: after portrait double click', 'scarecrow', sc.get('s'), 'units', [u.get('s') for u in us])
    for vk_up, vk_dn in ((0x26, 0x28), (0x57, 0x53)):
        for _ in range(60):
            y = sc['s'][1]
            if 150 < y < ch - 150: break
            w.a('key', vk=vk_up if y >= ch - 150 else vk_dn); time.sleep(0.15)
            sc2, us2 = look()
            if not sc2: break
            sc, us = sc2, us2
        log('attack: after keys', hex(vk_up), 'scarecrow', sc.get('s'))
        if 150 < sc['s'][1] < ch - 150: break
    try: w.shot(os.path.join(OUT[0], 'screen_before_order.png'))
    except Exception as e: log('shot:', e)
    sc, us = look() if look()[0] else (sc, us)
    for u in us:
        w.a('click', x=to(u['s'])[0], y=to(u['s'])[1]); time.sleep(1.0)
        w.a('click', x=to(sc['s'])[0], y=to(sc['s'])[1] - 2, button='right'); time.sleep(0.3)   # hover, then the order
        w.a('click', x=to(sc['s'])[0], y=to(sc['s'])[1], button='right'); time.sleep(1.0)
    w.a('click', x=to(sc['s'])[0], y=to(sc['s'])[1]); time.sleep(1.0)            # the scarecrow selected (the Mac flow)
    log('attack: ordered', len(us), 'unit(s); state', {k: call(d, 'tf_dps_test', 'state').get(k) for k in ('hits', 'bpanel_on', 'sr')})


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='dps_check_vm_out'); ap.add_argument('--probe')
    ap.add_argument('--secs', type=int, default=60)
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True); OUT[0] = out
    probe = a.probe
    if not probe:
        probe = os.path.join(out, 'probe_build')
        subprocess.run(['zsh', os.path.join(HERE, 'units_probe', 'build.sh'), probe], check=True)
    w = W.Win(); w.up(); w.kill_all()
    md = W.ROOT + r'\mods\units-probe'
    lib_local, _ = U.make_library(out)
    meter = {}
    try:
        for f in ('drv', 'lib', 'wiki'):                                 # a fresh work folder (moved, never deleted)
            if w.exists(WORK + '\\' + f): w.move(WORK + '\\' + f, TO_DEL + '\\work_' + f)
        for f in ('custom-modes\\settings.json', 'custom-modes\\fights_gallery.txt', 'ui_places.tsv'):   # fresh Test Fights settings
            if w.exists(W.DATA + r'\Mods' + '\\' + f): w.move(W.DATA + r'\Mods' + '\\' + f, TO_DEL + '\\kept_' + f.replace('\\', '_'))
        for f in ('units-probe.dll', 'mod.json'):
            w.upload(os.path.join(probe, 'units-probe', f), md + '\\' + f)
        w.write(LIB, open(lib_local, 'rb').read())
        for rel in ('builder/data.js', 'builder/builder.js', 'builder/savegen.js', 'dbdata/gallery_codec.js'):
            w.upload(os.path.join(SITE, rel), WIKI + '\\' + rel.replace('/', '\\'))
        tag = re.search(r"TAG: '(\d+)'", open(os.path.join(SITE, 'builder', 'savegen.js')).read()).group(1)
        for f in sorted(os.listdir(os.path.join(SITE, 'assets', 'saves'))):   # the save generator's templates (this build)
            if f.endswith('_' + tag + '.json'):
                w.upload(os.path.join(SITE, 'assets', 'saves', f), WIKI + r'\assets\saves' + '\\' + f)
        env = {'DSL_CLOUD': '0', 'DSL_UNITS_TEST': DRV, 'DSMP_UNITS_LIB': LIB, 'DSCM_WIKI_DIR': WIKI}
        w.prepare(lang=0, consent='no', active=['multiplayer', 'custom-modes', 'units-probe'], settings={})
        w.write(DRV + r'\.keep', b'')
        w.start(env=env)
        d = U.Drv(w)
        d('open'); time.sleep(3)
        ok = d.click('home:Test Fights', 3)
        try: s = w.wait(lambda: (lambda t: t if t.get('engine') in (2, 3) else None)(tf_state(d)), 'Test Fights engine', 300, 2)
        except W.GameError: s = tf_state(d)
        check('Units Laboratory > Test Fights opened, the wiki\'s fight maker ready', ok and s.get('engine') == 2, (ok, s.get('engine'), s.get('status')))
        d.click('fights:l.team0', 2)
        t = tf_state(d)
        for i in range(6):                                               # the Test row: DPS Check (index by the wiki's mode list)
            if t.get('mode') == 'dps': break
            d('call', fn='cmf_test', s=json.dumps({'cmd': 'set', 'name': 'fights:r.mode', 'index': i}, separators=(',', ':'))); time.sleep(0.8)
            t = tf_state(d)
        check('a team is the squad, mode DPS Check', (t.get('squad') or 0) > 0 and t.get('mode') == 'dps', {k: t.get(k) for k in ('squad', 'mode')})
        d.click('fights:start', 1)
        try: w.wait(lambda: (lambda x: x.get('on') == 1 and x.get('title') == 0)(call(d, 'tf_dps_test', 'state')), 'the DPS Check world', 600, 3)
        except W.GameError: pass
        st = call(d, 'tf_dps_test', 'state')
        check('the DPS Check world loaded', st.get('on') == 1 and st.get('title') == 0, {k: st.get(k) for k in ('on', 'title', 'hits')})
        attack(w, d)
        t0 = time.time(); alive = True
        while time.time() - t0 < a.secs:
            time.sleep(10)
            alive = alive and w.alive()
            log('t+%ds hits %s alive %s' % (time.time() - t0, call(d, 'tf_dps_test', 'state').get('hits'), alive))
        meter = call(d, 'tf_meter_test', 'state')
        st = call(d, 'tf_dps_test', 'state')
        check(f'the game ran {a.secs} s in the DPS Check world: no crash, no hang', alive and w.alive())
        rows = [(u.get('name'), u.get('hits'), round(u.get('dmg') or 0)) for u in meter.get('units') or []]
        check('the DPS meter counts damage: unit rows with hits > 0', any((h or 0) > 0 for _, h, _ in rows) and (meter.get('total') or 0) > 0,
              {'rows': rows, 'total': meter.get('total'), 'hits': meter.get('hits'), 'dps_hits': st.get('hits')})
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        for p in (md + r'\units-probe.dll', md + r'\mod.json'):          # moved away, never deleted
            try:
                if w.exists(p): w.move(p, TO_DEL + '\\' + p.split('\\')[-1])
            except Exception as e: log('move:', e)
        json.dump({'checks': checks, 'meter': meter}, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    try: L = open(os.path.join(out, 'dsload.log'), encoding='utf-8', errors='replace').read().splitlines()
    except OSError: L = []
    hook = [l for l in L if 'tf: DPS Check: start move' in l and ('(0)' in l or 'refused' in l or ' off ' in l)]
    check('dsload.log: the start move hook on ControlModeProcessor.SwitchMode is made ("start move on (0)", not refused)',
          any('start move on (0)' in l for l in hook) and not any('refused' in l or ' off ' in l for l in hook), hook[:3])
    found = [l for l in L if "tf: DPS Check: start move: the map's orders found" in l]
    check('dsload.log: "tf: DPS Check: start move: the map\'s orders found"', bool(found), len(found))
    sent = [l for l in L if 'tf: DPS Check: start move (' in l and 'squad unit(s)' in l]   # 0.1.49: "start move (own tile|next to the scarecrow, how): N of M squad unit(s), ..."
    log('start move sent:', sent[:2])
    n_sent = [int(m.group(1)) for l in sent for m in [re.search(r': (\d+) of \d+ squad unit', l)] if m]
    check('dsload.log: the start move gave its order to the squad units ("N of M squad unit(s)", N > 0)', any(n > 0 for n in n_sent), sent[:2])
    refused = [l for l in L if 'refused' in l and 'scan guard' not in l]   # the loader's own test-only guard
    log('loader test-copy guard lines (not a mod hook):', [l[:160] for l in L if 'refused' in l and 'scan guard' in l][:2])
    check('dsload.log: no mod hook refused (no custom-modes hook on a shared stub)', not refused, refused[:5])
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
