#!/usr/bin/env python3
"""lan_v10 menu lane, ux_flow F17 + F9 on a leased pair (lease v10menu; core API in mp_game.c, mpmenu boxes).
  teams_invite_pair.py [--dylib PATH]
X hosts MoonlightBrigade (mp.json). Y has no mp.json.
  F17  Y's Recent worlds row is for another world (key gid 0000...): a click is refused, "The owner is playing another world
       now (...), not Old World.", nothing loads.
  F9   Y plays its own world (single player). An accepted invite (mp_test_invite, the rig has no Steam) asks first:
       "Join the owner?" [Cancel] [Save and join]. Escape = Cancel, Cancel keeps the world; Save and join saves, shows
       "Joining the owner's game..." with Cancel, then Y is in the owner's world.
Frames research/ux_shots/v10_invite_*.png."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check = T.rig, T.log, T.c, T.st, T.wait, T.check
results = T.results
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/research/ux_shots'))
import ux
B = ux.buttons()
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')


def shot(x, n): c(x, cmd='mpm_shot', how='backbuffer', path=f'{OUT}/v10_invite_{n}.png')
def ms(x): return c(x, cmd='mpm_status')
def press(x, b, t=1.5): c(x, cmd='mpm_press', button=B[b]); time.sleep(t)


def aside(p):
    if os.path.exists(p): os.rename(p, p + '.prev_' + time.strftime('%Y%m%d_%H%M%S'))


def main():
    save = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/v10menu/dsrig_v10menu_mouse.dylib'))
    pair = rig.lease('v10menu', os.getpid())
    if not pair: raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]; port = rig.PAIR_PORTS.get(pair, 7795)
    try:
        assert rig.check_prefs()
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        for f in ('mp.json', 'mp_history.json'): aside(os.path.join(rig.idir(Y), f))
        with open(os.path.join(rig.idir(Y), 'mp_history.json'), 'w') as f:
            f.write('[\n' + json.dumps({'key': f'00000000-0000-0000-0000-000000000001|ip|127.0.0.1:{port}', 'world': 'Old World', 'day': 5, 'how': 'ip',
                                        'addr': f'127.0.0.1:{port}', 'host': 'The owner', 'steamid': '0', 'players': 'The owner|0;Ben|1', 'units': '', 'portraits': '',
                                        'when': int(time.time())}) + '\n]\n')
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True); src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        c(X, cmd='load_save', file=src, det=1); wait('X locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        time.sleep(4)
        # F17
        press(Y, 'MULTIPLAYER', 2); press(Y, 'HIST0', 1)
        m = wait('refusal', lambda: (lambda q: q if q['msg'] and not q['connecting'] else None)(ms(Y)), timeout=60, every=1) or ms(Y)
        check('F17: a Recent worlds row for another world is refused with the host\'s world named, nothing loads',
              'The owner is playing another world now (' in m['msg'] and 'not Old World.' in m['msg'] and m['obs']['scene'] == 1 and m['obs']['role'] == 0, m['msg'])
        shot(Y, 'other_world_guest')
        press(Y, 'BACK', 1)
        # F9: Y in its own world
        wy = os.path.join(rig.idir(Y), 'save_in'); os.makedirs(wy, exist_ok=True); sy = os.path.join(wy, os.path.basename(save)); shutil.copy2(save, sy)
        c(Y, cmd='load_save', file=sy)
        wait('Y in its own world', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', 0) > 0)(st(Y)), timeout=900)
        time.sleep(5)
        inv = lambda: c(Y, cmd='mp_test_invite', port=port, **{'from': 'The owner'})
        r = inv(); time.sleep(2); m = ms(Y)
        check('F9: an invite during a single-player game asks first: "Join the owner?" [Cancel] [Save and join]; nothing joined yet',
              r.get('pending') == 1 and m['box']['on'] and 'Join the owner?' in m['box']['text'] and m['box']['confirm'] == 'Save and join' and m['box']['cancel'] == 'Cancel'
              and m['obs']['role'] == 0, f"{r} {json.dumps(m['box'])}")
        shot(Y, 'ask_guest')
        e = c(Y, cmd='mpm_escape_ours'); time.sleep(2); w = c(Y, cmd='mp_world'); m = ms(Y)
        check('Escape = Cancel: box closed, no join, the world stays', e.get('taken') == 1 and not m['box']['on'] and w.get('invite_pending') == 0 and not w.get('session') and st(Y)['scene'] == 2, f"{e} {w}")
        inv(); time.sleep(2); c(Y, cmd='mpm_box', action='cancel'); time.sleep(2); w = c(Y, cmd='mp_world')
        check('Cancel: no join, the world stays', w.get('invite_pending') == 0 and not w.get('session') and not ms(Y)['box']['on'], str(w))
        # a failed save: the Saves folder of this test copy is made read-only for a moment (restored right after)
        sdir = os.path.join(rig.idir(Y), 'data', 'Saves'); mode0 = os.stat(sdir).st_mode & 0o777
        inv(); time.sleep(2); os.chmod(sdir, 0o555)
        try:
            c(Y, cmd='mpm_box', action='confirm'); time.sleep(3); m = ms(Y); w = c(Y, cmd='mp_world')
        finally:
            os.chmod(sdir, mode0)
        saved = [l.strip() for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') if 'save before joining' in l][-1:]
        if saved and 'FAILED' in saved[0]:
            check('failed save: a second box "Your game could not be saved. Join the owner\'s game anyway?" [Cancel] [Join anyway]; the invite stays open',
                  m['box']['on'] and 'could not be saved' in m['box']['text'] and m['box']['confirm'] == 'Join anyway' and w.get('invite_pending') == 1, f"{json.dumps(m['box'])} {w}")
            shot(Y, 'nosave_guest')
            c(Y, cmd='mpm_box', action='cancel'); time.sleep(2); w = c(Y, cmd='mp_world')
            check('... Cancel there: no join, the world stays', w.get('invite_pending') == 0 and not w.get('session') and not ms(Y)['box']['on'], str(w))
        else:
            log('NOTE: the read-only folder did not make the autosave fail', saved, json.dumps(m['box']))
            if w.get('session') or w.get('invite_pending') == 0:
                raise SystemExit('the save worked and the join started: cannot test the failed save this way')
        inv(); time.sleep(2); c(Y, cmd='mpm_box', action='confirm'); time.sleep(0.5)
        seen = None
        for _ in range(40):
            m = ms(Y)
            if m['box']['on'] and 'Joining the owner' in m['box']['text']: seen = m['box']; shot(Y, 'joining_guest'); break
            time.sleep(0.25)
        wait('Y in the owner\'s world', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        log('Y rig.log save line', [l.strip() for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') if 'save before joining' in l][-1:])
        check('Save and join: autosave, "Joining the owner\'s game..." with Cancel while it connects / loads, then Y is in the owner\'s world',
              seen is not None and seen['confirm'] == 'Cancel' and any('autosave written' in l for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace')),
              json.dumps(seen))
        time.sleep(3); m = ms(Y)
        check('after the join: no box left', not m['box']['on'], json.dumps(m['box']))
    finally:
        for i in pair:
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
