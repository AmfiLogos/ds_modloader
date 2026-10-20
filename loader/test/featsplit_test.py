#!/usr/bin/env python3
"""Launcher features vs game mods (owner 2026-10-20) on test copy S (hidden, muted, staged; take a lease first). Uses
this worktree's build/ (dsload.dylib; the mods already in the copy: multiplayer, custom-modes = Units Laboratory, gcd-cdr).
 run 1: Units Laboratory OFF, the world saved WITH it (worlds.json "custom-modes+gcd-cdr"):
        the Mods window has two groups (game mods, launcher features); the save loads with no "other mods" refusal;
        the stored key loses custom-modes.
 run 2: Units Laboratory ON, the world saved WITHOUT it ("gcd-cdr"): the row shows only gcd-cdr, the card names the
        features in their own block; the world's Mods window ([Change]) lists no feature; the save loads, no refusal.
Breaks caught: a world refused ("This world uses other mods") because of a launcher feature; features shown as world mods.
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, glob, shutil, signal, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_featsplit/out/featsplit')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
FEAT = {'multiplayer', 'custom-modes'}


def worlds():
    return json.load(open(os.path.join(T.MODS, 'worlds.json'))).get('worlds', {})


def quit_all():
    for p in T.game_pids():
        try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        if T.alive(p): os.kill(p, signal.SIGKILL)
    for f in ['dsload.log', 'dsload.prev.log']:
        if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f'{f}.{RUN[0]}'))


RUN = [0]
def one_run(n, enabled, key, S):
    RUN[0] = n
    T.start_a(enabled=enabled, extra_env={'DSL_UI_TEST': V.UI}, worlds={T.GUID: key})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), f'run {n} title'); time.sleep(3)
    if n == 1:
        V.ui('open'); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        names = [c['name'] for c in st.get('controls', [])]
        S['run1_controls'] = names
        sg, sf = names.index('section:game') if 'section:game' in names else -1, names.index('section:features') if 'section:features' in names else -1
        game = [x[4:] for x in names[sg:sf] if x.startswith('mod:')] if sg >= 0 and sf > sg else []
        feat = [x[4:] for x in names[sf:] if x.startswith('mod:')] if sf >= 0 else []
        S['run1_game'], S['run1_feat'] = game, feat
        check('Mods window: two groups, game mods first', sg >= 0 and sf > sg, names)
        check('launcher features group = Multiplayer + Units Laboratory', set(feat) == FEAT, feat)
        check('game group has no feature', game and not (set(game) & FEAT), game)
        T.shot('1_mods_window')
        V.ui('close'); time.sleep(1)
        check('stored key lost custom-modes at start', worlds().get(T.GUID) == 'gcd-cdr', worlds())
    else:
        r.send(T.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', T.wait_log('rows: Load list rebuilt', 20)); time.sleep(1.5)
        mark = len(T.dlog()); m, text = V.tip('row:0', '2_row_card')
        b = V.wait_line(r"reg test: tip row:0 body '([^']*)'", mark, 5)
        body = b.group(1) if b else ''
        S['run2_row'], S['run2_card'] = text, body
        check('row line names only the game mod', text and 'Global cooldown' in text and 'Multiplayer' not in text and 'Laboratory' not in text and '+' not in text.split('Global cooldown')[-1], text)
        check('card: features in their own block', 'Launcher features' in body and 'Multiplayer' in body and 'Units Laboratory' in body
              and body.index('Global cooldown') < body.index('Launcher features'), body)
        T.request({'cmd': 'change', 'row': 0}); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        names = [c['name'] for c in st.get('controls', [])]
        S['run2_world_controls'] = names
        check("world's Mods tab: no launcher feature, no group heading", 'mod:gcd-cdr' in names and not any(x[4:] in FEAT for x in names if x.startswith('mod:')) and 'section:features' not in names, names)
        T.shot('2_world_mods')
        V.ui('close'); time.sleep(1)
    mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0)
    ok = V.wait_line(r'world check: %s \(world [0-9a-f-]+\) uses this mod set: allowed' % T.SAVE, mark, 30)
    check(f'run {n}: the world loads, no "other mods" refusal', ok is not None and 'REFUSED' not in V.since(mark), V.since(mark)[-400:])
    try:
        T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), f'run {n} in the world', 90)
        check(f'run {n}: in the world', True); T.shot(f'{n}_in_world')
    except Exception as e:
        check(f'run {n}: in the world', False, repr(e))
    quit_all()


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    for f in glob.glob(os.path.join(T.I, 'data', 'Saves', '*')) + glob.glob(os.path.join(T.I, 'mp.json')):
        os.makedirs(os.path.join(T.I, 'old'), exist_ok=True); os.replace(f, os.path.join(T.I, 'old', os.path.basename(f) + '.%d' % time.time()))
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        one_run(1, ('multiplayer', 'gcd-cdr'), 'custom-modes+gcd-cdr', S)
        one_run(2, ('multiplayer', 'custom-modes', 'gcd-cdr'), 'gcd-cdr', S)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        quit_all()
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
