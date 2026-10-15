#!/usr/bin/env python3
"""lan_v10 menu + teams lane on one leased hidden pair (lease name "v10menu"; dylib built with -DTEAMS_MOUSE_TEST).

  teams_v10_pair.py [--dylib PATH] [--keep]

X hosts MoonlightBrigade (The owner), Y joins (Friend). X takes f0 f3 f4, Y takes f1 f2.
  1 supplies   Y proposes f0 f3 f1 f2: X packs in the stock window, its title counts down ("Expedition Proposal · 52 s left").
  2 side panel Y (proposer) sees the side panel at the right edge; the world stays usable: a real right-click at the
               screen centre with the panel open sends a move order, the centre is not "over UI".
  3 receiver   X Next -> the panel with Approve / Decline; tooltips (Hide, time left, a player row, Approve...).
  4 fold       Escape (mpmenu_escape_ours, the ui lane's Escape hook asks it first) folds it; a real click on Show unfolds.
  5 timeout    the clock turns red in the last 10 s; at 60 s "No answer from the owner: expedition cancelled".
  6 approve    Y proposes again; X clicks Approve with the (fake) mouse: the panel folds, everyone leaves, fingerprints equal.
  7 rules      host changes Units per player and Admin mode in play: the guest gets a centred message (F12).
  8 leave      host Esc menu > Leave multiplayer: the box "End the game for everyone?"; Cancel and Escape keep the
               session; the guest quits (F10: host status "Waiting for Friend · 0:05 · press Play ..."); End -> alone.
Frames: research/ux_shots/v10_*.png."""
import json, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle, T.fp_equal_from
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/research/ux_shots'))
MPB = None


def shot(x, name):
    p = f'{OUT}/v10_{name}.png'
    log('frame', p, 'rc', tm(x, action='shot', path=p).get('rc'))


def ms(x):
    return c(x, cmd='mpm_status')


def secs(title):
    m = re.search(r'(\d+) s left', title or '')
    return int(m.group(1)) if m else None


def centre(r):
    return r[0] + r[2] / 2, r[1] + r[3] / 2


def mouse(who, px, py, button=0):
    return rig.send(who, {'cmd': 'ui_teams', 'action': 'mouse', 'x': px, 'y': py, 'button': button}, timeout=120)


def click(x, r, button=0):
    cx, cy = centre(r)
    mouse(x, cx, cy, button)
    time.sleep(2.5)


def captured(x):
    return c(x, cmd='ord_stats').get('captured', -1)


def take(x, guids):
    for g in guids:
        tm(x, action='click', guid=g, button=0); time.sleep(1.5)


def button_ids():
    import ux
    return ux.buttons()


def main():
    global MPB
    MPB = button_ids()
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/v10menu/dsrig_v10menu_mouse.dylib'))
    pair = rig.lease('v10menu', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        a = tm(X)
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('5 free units', len(free) >= 5, f'{len(free)}')
        f = free[:5]
        t0 = st(X)['tick']
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        take(X, [f[0], f[3], f[4]]); take(Y, [f[1], f[2]])
        settle(X, Y, lambda a, b: all(col_of(a, g) == 0 for g in (f[0], f[3], f[4])) and all(col_of(b, g) == 1 for g in (f[1], f[2])), 'takes', timeout=60)
        tm(X, action='close'); tm(Y, action='close'); time.sleep(2)
        party = [f[0], f[3], f[1], f[2]]

        # 1 + 2: Y proposes; X packs with a countdown; Y's side panel leaves the world usable.
        # X has the game's Esc menu open when the proposal comes (uxgame 17:15: the summary never showed then)
        c(X, cmd='mpm_esc'); time.sleep(2)
        T.propose(Y, party)
        a, b = settle(X, Y, lambda a, b: a['stock']['open'] and a['stock']['mode'] == 2 and b['prop']['shown'], 'receiver supplies', timeout=60)
        s1 = secs(a['stock']['time']); time.sleep(3.5); a = tm(X); s2 = secs(a['stock']['time'])
        check('F5 supplies: the receiver\'s Going list counts down ("N s left to pack and answer"), title unchanged',
              a['stock']['title'] == 'Expedition Proposal' and s1 is not None and s2 is not None and 0 < s2 < s1 <= 60 and 'to pack and answer' in a['stock']['time'],
              f"{a['stock']['title']!r} {a['stock']['time']!r} {s1} -> {s2}")
        tm(X, action='tip', what='pack_time'); time.sleep(0.5); tp_ = tm(X)['tip']
        check('tooltip on the time line', tp_ == 'Time Left', repr(tp_))
        time.sleep(1.5); shot(X, 'supplies_countdown_host')
        b = tm(Y); p = b['prop']
        W = None
        try:
            W = c(Y, cmd='status').get('screen', {}).get('w')
        except Exception:
            pass
        check('F4 proposer: the side panel shows (Cancel, time left), not modal: modal flag off',
              p['shown'] and p['no'] == 'Cancel' and p['ok'] == '' and p['rect'] and p['time'].endswith('s left') and not b['open'], json.dumps(p)[:600])
        r = p['rect']
        check('side panel: at the right edge, under a third of the screen wide', r[0] > 0 and r[2] < 520 and r[1] > 0, f'rect {r} screen w {W}')
        shot(Y, 'panel_proposer_guest')
        b0, b1 = captured(Y), None
        T.c(Y, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': f[1]}); time.sleep(8)
        b0 = captured(Y)
        tm(Y, action='mouse', button=1); time.sleep(3)
        sy = tm(Y); time.sleep(9); b1 = captured(Y)
        check('F4: with the panel open, a right-click in the world sends a move order (centre not over UI)',
              b1 > b0 and sy['over_ui'] == 0 and sy['prop']['shown'], f"captured {b0} -> {b1}, over_ui {sy['over_ui']} at {sy['mouse']}")
        pr = tm(Y)['prop']['rect']
        mouse(Y, centre(pr)[0], centre(pr)[1], 1); time.sleep(3)
        sy = tm(Y); time.sleep(4)
        check('the panel itself is "over UI" (a click on it never reaches the world)', sy['over_ui'] == 1, f"over_ui {sy['over_ui']} at {sy['mouse']}")

        # 3: receiver Next -> panel with Approve / Decline; tooltips
        tm(X, action='stock_next')
        a, b = settle(X, Y, lambda a, b: a['prop']['shown'] and a['prop']['ok'] == 'Approve', 'receiver panel', timeout=60)
        esc_x = c(X, cmd='mpm_status')['obs']['esc']
        check('receiver with the Esc menu open: Next -> the side panel with Approve / Decline, 4 / 4 members, stock window closed',
              esc_x == 1 and a['prop']['no'] == 'Decline' and not a['stock']['open'] and a['prop']['members'] == 4 and a['prop']['rows'] == 2, json.dumps(a['prop'])[:500])
        time.sleep(1); shot(X, 'panel_receiver_esc_host')
        vx = c(X, cmd='mpm_status')
        if vx['obs']['esc']:                                     # close the Esc menu the way a player does (its Return to Game)
            T.c(X, cmd='mpm_return_game'); time.sleep(2)
        time.sleep(1); shot(X, 'panel_receiver_host')
        tt = {}
        for w_, pl_ in (('fold', 0), ('time', 0), ('row', 0), ('row', 1), ('approve', 0), ('decline', 0), ('region', 0), ('waiting', 0)):
            tm(X, action='tip', what=w_, player=pl_); time.sleep(0.5); tt[f'{w_}{pl_ if w_ == "row" else ""}'] = tm(X)['tip']
        tm(X, action='tip', what='fold'); time.sleep(1.5); shot(X, 'tip_fold_host')
        tm(X, action='tip', what='row', player=1); time.sleep(1.5); shot(X, 'tip_row_host')
        check('panel tooltips: Hide, Time Left, both player rows, Approve, Decline, Region, Waiting',
              tt.get('fold') == 'Hide' and tt.get('time') == 'Time Left' and tt.get('row0') == 'Friend' and tt.get('row1') == 'The owner (You)'
              and tt.get('approve') == 'Approve' and tt.get('decline') == 'Decline' and tt.get('region') == 'Region' and tt.get('waiting') == 'Waiting', json.dumps(tt))

        # 4: Escape folds; a click on Show unfolds
        e1 = c(X, cmd='mpm_escape_ours'); time.sleep(1); a = tm(X)
        check('F15 Escape folds the panel (taken by ours, the game\'s Esc menu stays shut)', e1.get('taken') == 1 and a['prop']['folded'] == 1 and a['prop']['ok'] == '', f"{e1} {json.dumps(a['prop'])[:300]}")
        time.sleep(1); shot(X, 'panel_folded_host')
        e2 = c(X, cmd='mpm_escape_ours')
        check('a second Escape is the game\'s again (panel already folded)', e2.get('taken') == 0, str(e2))
        a = tm(X); click(X, a['prop']['fold_rect']); a = tm(X)
        check('a click on "Show" unfolds it (real input path)', a['prop']['folded'] == 0 and a['prop']['ok'] == 'Approve', json.dumps(a['prop'])[:300])

        # 5: the last 10 s in red, then the host's timeout
        wait('10 s left', lambda: (lambda p: p['active'] and p['secs'] <= 9)(tm(X)['prop']), timeout=90, every=1)
        shot(X, 'panel_last10_host'); shot(Y, 'panel_last10_guest')
        a = tm(X)
        settle(X, Y, lambda a, b: not a['prop']['active'] and not b['prop']['active'], 'timeout', timeout=40)
        time.sleep(2); a, b = tm(X), tm(Y)
        want_ev = 'No answer from the owner: expedition cancelled'
        check('timeout: the panel closes on both, "No answer from the owner: expedition cancelled", nobody ready',
              not a['prop']['shown'] and not b['prop']['shown'] and a['prop']['event'] == want_ev and b['prop']['event'] == want_ev and a['ready'] == 0 and b['ready'] == 0,
              f"{a['prop']['event']!r} / {b['prop']['event']!r}")

        # 6: approve with a real click
        T.propose(Y, party)
        settle(X, Y, lambda a, b: a['stock']['open'] and a['stock']['mode'] == 2, 'proposal 2', timeout=60)
        reg = tm(Y)['prop']['region']
        tm(X, action='stock_next')
        a, b = settle(X, Y, lambda a, b: a['prop']['shown'] and a['prop']['ok'] == 'Approve' and a['prop']['ok_rect'], 'receiver panel 2', timeout=60)
        click(X, a['prop']['ok_rect'])
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('Approve by a real click: EXPEDITION_START on both, 4 units, the proposal\'s region',
              a['starts'] == 1 and b['starts'] == 1 and a['start_units'] == 4 and a['prop']['start_region'] == reg and b['prop']['start_region'] == reg,
              f"starts {a['starts']}/{b['starts']} units {a['start_units']} region {a['prop']['start_region']} want {reg}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        t_land = max(st(X)['tick'], st(Y)['tick'])
        wait('300 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 300, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n >= 300 and first is None, f'common ticks {n}, first diff {first}')

        # 7: rule changes in play are told to the guest (F12)
        c(X, cmd='mpm_rules', units=2); wait('guest message (units)', lambda: 'Units per player is now 2' in ms(Y).get('message', ''), timeout=30, every=1)
        m1 = ms(Y).get('message', ''); shot(Y, 'rule_units_guest')
        tm(X, action='admin', on=1); wait('guest message (admin)', lambda: 'Admin mode on' in ms(Y).get('message', ''), timeout=30, every=1)
        m2 = ms(Y).get('message', ''); time.sleep(0.5); shot(Y, 'rule_admin_guest')
        check('F12: the guest is told when the host changes Units per player and Admin mode in play (host: no message)',
              'Units per player is now 2 (host rule)' in m1 and 'The owner</color> turned Admin mode on: the host can control every unit.' in m2 and ms(X).get('message', '') == '',
              f'{m1!r} / {m2!r} / host {ms(X).get("message")!r}')
        tm(X, action='admin', on=0); c(X, cmd='mpm_rules', units=3); time.sleep(3)

        # 8: host Leave asks first
        c(X, cmd='mpm_esc'); time.sleep(2)
        c(X, cmd='mpm_press', button=MPB['LEAVE']); time.sleep(2)
        m = ms(X)
        check('F7 host Leave: the box "End the game for everyone?" names the guest; buttons End for everyone / Cancel; still in session',
              m['box']['on'] and 'End the game for everyone?' in m['box']['text'] and 'Friend' in m['box']['text'] and m['box']['confirm'] == 'End for everyone'
              and m['box']['cancel'] == 'Cancel' and m['confirm_leave'] == 1 and m['obs']['role'] == 1, json.dumps(m['box']))
        shot(X, 'leave_box_host')
        c(X, cmd='mpm_box', action='cancel'); time.sleep(2); m = ms(X)
        check('Cancel: box closed, the session goes on', not m['box']['on'] and m['confirm_leave'] == 0 and m['obs']['role'] == 1 and st(Y).get('scene') == 2, json.dumps(m['box']))
        c(X, cmd='mpm_press', button=MPB['LEAVE']); time.sleep(2)
        e = c(X, cmd='mpm_escape_ours'); time.sleep(2); m = ms(X)
        check('Escape = Cancel in the box', e.get('taken') == 1 and not m['box']['on'] and m['confirm_leave'] == 0 and m['obs']['role'] == 1, f"{e} {json.dumps(m['box'])}")
        # F10: the guest drops; the host waits; the status says the time and that Play keeps playing
        log('guest app quit', c(Y, cmd='mp_quit', mode='app'))
        wait('host prompt', lambda: c(X, cmd='ui_view').get('left_prompt') == 1, timeout=30, every=1)
        c(X, cmd='ui_notice', button='confirm'); time.sleep(6)
        vs = c(X, cmd='ui_view').get('status', '')
        check('F10 host after Wait: "Waiting for Friend \u00b7 m:ss \u00b7 press Play to keep playing with Friend\'s units"',
              re.search(r'Waiting for Friend \u00b7 \d:\d\d \u00b7 press Play', vs) is not None, repr(vs))
        shot(X, 'wait_hint_host')
        c(X, cmd='mpm_esc'); time.sleep(2)
        c(X, cmd='mpm_press', button=MPB['LEAVE']); time.sleep(2); m = ms(X)
        check('Leave with nobody else here: "Leave multiplayer?" box', m['box']['on'] and 'Nobody else is in this world now' in m['box']['text'], json.dumps(m['box']))
        c(X, cmd='mpm_box', action='confirm'); time.sleep(4); m = ms(X)
        check('End: the session ends, the host stays in the world, "You are playing alone now" message',
              m['state'] == 'idle' and m['obs']['role'] == 0 and st(X).get('scene') == 2 and 'playing alone now' in m.get('message', ''), f"{m['state']} role {m['obs']['role']} {m.get('message')!r}")
        shot(X, 'left_alone_host')
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
