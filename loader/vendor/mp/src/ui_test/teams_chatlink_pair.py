#!/usr/bin/env python3
"""lan_v10: a clickable chat system line (mpmenu_chat_system_line, for ui_game's pings) on a leased pair (mouse build).
  teams_chatlink_pair.py [--dylib PATH]
The host adds a linked line; a real (fake-mouse) left click on it calls ui_chat_link_click (dlsym). Before ui_game exports
it the click still arrives and the line turns plain (the "ping gone" path)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check = T.rig, T.log, T.c, T.st, T.wait, T.check
results = T.results


def main():
    save = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/v10menu/dsrig_v10menu_mouse.dylib'))
    pair = rig.lease('v10menu', os.getpid())
    if not pair: raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]; port = rig.PAIR_PORTS.get(pair, 7795)
    try:
        assert rig.check_prefs()
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True); src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        c(X, cmd='load_save', file=src, det=1); wait('locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        wait('Y loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(X if False else Y)), timeout=900)
        time.sleep(5)
        c(X, cmd='mpm_chat_sys', text='<color=#44AAFF>Ben</color><color=#a69085> pinged a spot.</color>', link=7); time.sleep(2)
        ch = c(X, cmd='mpm_status')['chat']
        check('the system line shows in the idle chat with its link and a screen rect', ch['links'] and ch['links'][0]['link'] == 7 and 'pinged a spot' in ch['last'][-1], json.dumps(ch['links']))
        T.shot(X, X, 'v10_chatlink') if False else c(X, cmd='mpm_shot', how='backbuffer', path=os.path.expanduser('~/ds_multiplayer/research/ux_shots/v10_chatlink_host.png'))
        r = ch['links'][0]['rect']
        units = [u['guid'] for u in c(X, cmd='list_units', map=0).get('units', []) if u['faction'] == 1 and not u['dead']]
        def sel(): return c(X, cmd='ord_stats').get('sel', [])
        def mouse(px, py): return rig.send(X, {'cmd': 'ui_teams', 'action': 'mouse', 'x': px, 'y': py, 'button': 0}, timeout=60)
        # the test is sensitive: a left click on empty world clears the selection
        c(X, cmd='ord_select', guids=units[0]); time.sleep(2); s0 = sel()
        mouse(620, 180); time.sleep(2); co0 = c(X, cmd='mpm_status')['chat']['over']; time.sleep(2); s1 = sel()
        check('mpmenu_chat_over() = 0 with the mouse on the world', co0 == 0, co0)
        check('baseline: a unit is selected, and a left click on the world changes the selection', len(s0) == 1 and s1 != s0, f'{s0} -> {s1}')
        c(X, cmd='mpm_chat_sys', text='<color=#44AAFF>Ben</color><color=#a69085> pinged a spot.</color>', link=8); time.sleep(2)
        r = next(l['rect'] for l in c(X, cmd='mpm_status')['chat']['links'] if l['link'] == 8)   # line 7 may be fading out
        c(X, cmd='ord_select', guids=units[0]); time.sleep(1); s2 = sel()
        mouse((r[0] + r[2]) / 2, (r[1] + r[3]) / 2); time.sleep(3)
        ov = c(X, cmd='ui_teams'); co = c(X, cmd='mpm_status')['chat']['over']; time.sleep(1); s3 = sel()
        check('mpmenu_chat_over() = 1 with the mouse on the line', co == 1, co)
        check('a left click on the chat line is a UI click: over UI, the world selection stays', len(s2) == 1 and s3 == s2 and ov['over_ui'] == 1,
              f"sel {s2} -> {s3}, over_ui {ov['over_ui']} raw {ov['over_ui_raycast']} at {ov['mouse']}")
        ch2 = c(X, cmd='mpm_status')['chat']
        with open(os.path.join(rig.idir(X), 'rig.log'), errors='replace') as f: tail = [l for l in f if 'chat link' in l][-2:]
        check('a left click on it reaches ui_chat_link_click (or turns the line plain if the ping is gone)', ch2['link_clicks'] >= 1, f"{ch2['link_clicks']} {tail} links {ch2['links']}")
    finally:
        for i in pair:
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
