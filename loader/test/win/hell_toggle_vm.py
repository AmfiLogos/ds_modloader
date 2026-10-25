#!/usr/bin/env python3
"""Windows VM run of the Hell switch (test/hell_toggle_test.py on the Mac, its core; replaces hell_newgame_vm.py's
10-20 run): Hell always loaded, the New Game screen's switch under "Custom difficulty" only shows or hides level 5.
Through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up and the kit under test
installed (hell-difficulty in it, NOT in the test profile). One game process all along.
  python3 hell_toggle_vm.py [--out DIR]
Real breaks it catches on Windows: Hell not running when installed; Hell offered with the switch off ("+" open at 4, a
level 5 kept); the switch on does not offer Hell or asks a restart; a new game on Hell does not start (the 10-20 loading
hang) or is not registered with hell-difficulty / its save not on combat 5; the multiplayer set has Hell at the title.
New Game requests: the loader's test file (DSL_SOFT_TEST) softrestart\\reg_request.json {"cmd":"newgame","op":...}.
What the profile had (newgame.json) is moved aside; test saves stay in the test profile. Exit 0 = every check passed."""
import argparse, json, os, sys, time

sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

HELL = 'hell-difficulty'
NGJ = W.DATA + r'\Mods\newgame.json'
OUTGAME = W.DATA + r'\OutGame.json'                                 # DifficultyUnlockSaveData.IsDownfallUnlocked (false in a fresh profile)
STATE = W.PROF + r'\softrestart\newgame_state.json'
REQ = W.PROF + r'\softrestart\reg_request.json'
TO_DEL = W.WORK + r'\to_delete\hell_toggle_vm_' + time.strftime('%Y%m%d-%H%M%S')
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


class NG:
    def __init__(self, w): self.w = w

    def __call__(self, op, **kw):
        """one New Game request; returns the state file written after it ran"""
        w = self.w
        if w.exists(STATE): w.move(STATE, TO_DEL + r'\newgame_state_prev.json')   # the agent's mtime is whole seconds: a new file = the answer
        w.write(REQ, json.dumps(dict(kw, cmd='newgame', op=op)))
        return w.wait(lambda: w.exists(STATE) and w.json_file(STATE), 'newgame state after ' + op, 15, 0.2)

    def wait_state(self, pred, what, limit=60):
        t0 = time.time(); s = {}
        while time.time() - t0 < limit:
            s = self('state')
            if pred(s): return s
            time.sleep(0.5)
        raise W.GameError(f'timeout waiting for {what}: {s}')

    def to_settings(self):
        self('open')
        self.wait_state(lambda s: s['open'] and s['view_type'] == 0, 'New Game screen', 30)
        time.sleep(1)
        self('next')
        return self.wait_state(lambda s: s['settings_shown'] and s['view_type'] == 2 and s['toggles'] and s['toggles'][0]['shown'],
                               'settings panel with the switch', 30)


def tog(s): return (s.get('toggles') or [{}])[0]


def run(w, out):
    ng = NG(w)
    sess = lambda: w.json_file(W.DATA + r'\Mods\worlds.json') or {}
    hello_hell = lambda: any(l.startswith(HELL + '@') for l in sess().get('modset_lines', []))
    pid0 = w.pid
    L = w.text(W.PROF + r'\dsload.log') or ''
    check('Hell runs although the profile does not switch it on', f'{HELL}: custom difficulty: runs whenever installed' in L
          and 'Hell difficulty ready' in L and 'Hell tables ready' in L, [l for l in L.splitlines() if HELL in l][:6])
    check('title: the multiplayer set leaves Hell out', not hello_hell(), sess().get('modset_lines'))
    # 1 switch off (no newgame.json): no Hell
    ng.to_settings()
    ng('set', combat=4, management=4); time.sleep(1); s = ng('state'); t = tog(s)
    check('switch shown under Custom difficulty, off, Hell running', t.get('shown') == 1 and t.get('on') == 0 and t.get('running') == 1 and s.get('side_shown') == 1, (t, s.get('side_shown')))
    check('switch off: no Hell (levels stop at 4, both "+" locked at 4)',
          (s['max_level'], s['top_level'], s['combat'], s['management'], s['inc_combat'], s['inc_management']) == (4, 5, 4, 4, 0, 0), s)
    ng('set', combat=5, management=5); time.sleep(1); s = ng('state')
    check('switch off: asking for level 5 gives 4', (s['combat'], s['management']) == (4, 4), s)
    ng('force', combat=5); time.sleep(1.5); s = ng('state')
    check('switch off: a level 5 written past the selectors falls back to 4', s['combat'] == 4, s)
    try: w.gshot(os.path.join(out, 'hell_switch_off.png'), 'hell_off')
    except W.GameError as e: log('picture:', e)
    # 2 switch on: Hell offered, no restart
    mark = w.mark('dsload.log')
    ng('toggle', i=0); time.sleep(1.5); s = ng('state'); t = tog(s)
    check('switch on: saved (newgame.json), Hell offered ("+" open at 4)',
          t.get('on') == 1 and t.get('saved') == 1 and s['max_level'] == 5 and s['inc_combat'] == 1 and s['inc_management'] == 1, (t, s))
    ng('set', combat=5, management=4); time.sleep(1); s = ng('state')
    check('switch on: combat Hell (level 5) can be chosen', s['combat'] == 5 and s['combat_text'] and s['combat_text'] != s['management_text'], s)
    nj = w.json_file(NGJ) or {}
    check('newgame.json says Hell shown', (nj.get('show') or {}).get(HELL) is True, nj)
    since = w.read(W.PROF + r'\dsload.log', offset=mark)[0].decode('utf-8', 'replace')
    check('switch on: no restart asked (no Apply, no soft restart), same game process',
          'reg: Apply' not in since and 'soft restart' not in since and w.pid == pid0 and w.alive(), since[-400:])
    try: w.gshot(os.path.join(out, 'hell_switch_on.png'), 'hell_on')
    except W.GameError as e: log('picture:', e)
    # 3 a new game on combat Hell starts (the 10-20 Windows loading hang) and is a Hell world
    before = set((sess().get('worlds') or {}))
    ng('start')
    t0 = time.time(); new = []; st = {}
    while time.time() - t0 < 240:
        try:
            st = w.status(); w.cmd(cmd='mpm_skipintro', timeout=10)
        except W.GameError as e:
            st = {'err': str(e)}
        new = [g for g in (sess().get('worlds') or {}) if g not in before]
        if st.get('scene') == 2 and not st.get('busy') and not (st.get('map_loading') or {}).get('is_loading') and new: break
        time.sleep(2)
    time.sleep(2)
    loaded = st.get('scene') == 2 and bool(new)
    check('a new game on combat Hell starts: the world opens (no loading hang)', loaded and w.alive(), (time.time() - t0, st.get('scene'), st.get('busy'), new))
    g1 = new[0] if new else None
    check('the Hell world is registered with hell-difficulty', g1 and HELL in (sess().get('worlds') or {}).get(g1, '').split('+'), (g1, (sess().get('worlds') or {}).get(g1)))
    check('Hell world open: the multiplayer set has Hell', hello_hell(), sess().get('modset_lines'))
    if g1:
        w.cmd(cmd='mpm_quit_title', timeout=20)
        w.wait(w.on_title, 'the title after the Hell world', 120); time.sleep(3)
        hx = g1.replace('-', '')
        fs = sorted((e for e in w.a('ls', path=W.DATA + r'\Saves')['entries'] if hx in e['name'] and e['name'].endswith('.json')), key=lambda e: e['mtime'])
        cd = None
        if fs:
            d = w.json_file(W.DATA + '\\Saves\\' + fs[-1]['name']) or {}
            cd = (d.get('CampaignSaveHeader') or {}).get('CombatDifficulty')
        check('Hell world save: combat difficulty 5', cd == 5, (fs[-1]['name'] if fs else None, cd))
        check('back at the title: the multiplayer set leaves Hell out again', not hello_hell(), sess().get('modset_lines'))
        # 4 the switch kept its state; off with Hell chosen falls back to 4
        s = ng.to_settings(); t = tog(s)
        check('the switch kept its state (on)', t.get('on') == 1 and t.get('saved') == 1, t)
        ng('set', combat=5, management=4); time.sleep(1)
        ng('toggle', i=0); time.sleep(1.5); s = ng('state'); t = tog(s)
        check('switch off with combat Hell chosen: it falls back to 4, "+" locked again',
              t.get('on') == 0 and t.get('saved') == 0 and s['combat'] == 4 and s['inc_combat'] == 0, (t, s))
    check('still the first game process (no restart)', w.pid == pid0 and w.alive(), (pid0, w.procs()))
    pl = (w.text(W.PROF + r'\player.log') or '').splitlines()
    bad = [l for l in pl if 'KeyNotFoundException' in l or '[EnvHelper]' in l or 'InvalidCastException' in l]
    check('no KeyNotFound / EnvHelper / InvalidCast exception in player.log', not bad, bad[:3])


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='hell_toggle_vm_out'); a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    w = W.Win(); w.up(); w.kill_all()
    if w.exists(NGJ): w.move(NGJ, TO_DEL + r'\kept_newgame.json')       # off by default: no switch state from an earlier run
    og = w.read(OUTGAME)[0] if w.exists(OUTGAME) else None             # Devastation (4) locked = no switch (the game's own lock):
    if og is not None:                                                 # unlocked in the test profile for the run, put back after
        w.write(TO_DEL + r'\kept_OutGame.json', og)
        d = json.loads(og.decode('utf-8', 'replace').lstrip('\ufeff')); d.setdefault('DifficultyUnlockSaveData', {})['IsDownfallUnlocked'] = True
        w.write(OUTGAME, json.dumps(d, indent=2))
    try:
        w.prepare(lang=0, consent='no', active=['multiplayer'], settings={})
        w.start(env={'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1'})
        run(w, out)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        try:
            for f in ('newgame.json',):
                if w.exists(W.DATA + '\\Mods\\' + f): w.get(W.DATA + '\\Mods\\' + f, os.path.join(out, f))
        except Exception: pass
        w.stop(); w.kill_all()
        if w.exists(NGJ): w.move(NGJ, TO_DEL + r'\test_newgame.json')     # the next run starts with the switch off
        if og is not None: w.write(OUTGAME, og)                            # the profile's own unlock state back
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
