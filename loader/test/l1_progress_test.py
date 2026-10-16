#!/usr/bin/env python3
"""The progress bar of a soft restart (owner v13.3) on test copy S (hidden, muted, staged; take a lease first).
Uses this worktree's build/. The game's own frames are captured during each restart (DSL_SOFT_CAPTURE).
 1. Apply at the title (gcd-cdr): the old game draws the bottom strip; the steps come from the new game and only go up
 2. flow K into a world with other mods: the centered panel, "Opening your world" with the game's own load percent
Three frames are copied to <out>/shot_*.png (low and mid strip, mid panel)."""
import json, os, re, sys, time, glob, shutil, signal, threading, datetime
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/progress')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
CAP = os.path.join(OUT, 'cap'); os.makedirs(CAP, exist_ok=True)
DESIGN = os.environ.get('DESIGN', 'strip')                      # the title design to show: strip (B) or ornament (C)
check, log = T.check, T.log


def steps(text):
    """[(epoch, pct, step)] of the 'progress: N% text' lines"""
    day = datetime.date.today(); out = []
    for m in re.finditer(r'^(\d\d):(\d\d):(\d\d\.\d+) progress: (\d+)% (.*)$', text, re.M):
        t = datetime.datetime.combine(day, datetime.time(int(m.group(1)), int(m.group(2)))).timestamp() + float(m.group(3))
        out.append((t, int(m.group(4)), m.group(5)))
    return out


def frame_after(who, t, before=None):
    best = None
    for f in glob.glob(os.path.join(CAP, who + '_*.png')):
        ft = float(f.rsplit('_', 1)[1][:-4])
        if ft >= t and (before is None or ft < before) and (best is None or ft < best[0]): best = (ft, f)
    return best[1] if best else None


def shoot_when(name, pred, limit=60):
    """the old game's own frame (test request "shot") as soon as its last progress line matches pred(pct, step)"""
    t0 = time.time()
    while time.time() - t0 < limit:
        st = sorted(steps(V.logs_all()))
        if st and pred(st[-1][1], st[-1][2]):
            p = os.path.join(OUT, f'shot_{name}.png'); T.request({'cmd': 'shot', 'path': p})
            t1 = time.time()
            while time.time() - t1 < 3 and not os.path.exists(p): time.sleep(0.05)
            return (st[-1][1], st[-1][2]) if os.path.exists(p) else None
        time.sleep(0.05)
    return None


def round_logs(rnd):
    return ''.join(open(f, errors='replace').read() for f in glob.glob(os.path.join(OUT, f'round{rnd}', 'dsload*.log')))


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        a = T.start_a(extra_env={'DSL_PROGRESS_DESIGN': DESIGN})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        # 1 Apply at the title
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']})
        S['shot_1_strip_low'] = shoot_when(DESIGN + '_low', lambda p, s: 0 < p <= 20)
        S['shot_2_strip_mid'] = shoot_when(DESIGN + '_mid', lambda p, s: 30 <= p < 90)
        dt, b = T.wait_swap(a, t_req, 1); S['apply_seconds'] = round(dt, 2)
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 60); time.sleep(2)
        L1 = round_logs(1); st1 = sorted(steps(L1)); S['steps_apply'] = [(round(x - st1[0][0], 2), p, s) for x, p, s in st1] if st1 else []
        check('apply: design shown (' + DESIGN + ')', ('progress: bottom strip shown' if DESIGN == 'strip' else 'progress: ornament bar shown') in L1)
        check('apply: steps only go up, end at 100', st1 and all(st1[i][1] <= st1[i + 1][1] for i in range(len(st1) - 1)) and st1[-1][1] == 100, S['steps_apply'])
        names = {s for _, _, s in st1}
        check('apply: real steps (start, mods, game data, fonts, done)', {'Starting the game with the new mods', 'Loading fonts', 'Done'} <= names and any(s.startswith('Loading Global cooldown') for s in names), sorted(names))
        # 2 flow K
        T.request({'cmd': 'world_set', 'guid': T.GUID, 'ids': []}); time.sleep(1)
        r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0); time.sleep(2)
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'k_ok'})
        S['shot_3_panel_world'] = shoot_when('3_panel_world', lambda p, s: s == 'Opening your world' and p > 52, 90)
        dk, c = T.wait_swap(b, t_req, 2); S['flowK_seconds'] = round(dk, 2)
        T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), 'C in the world', 90); time.sleep(2)
        L2 = round_logs(2); st2 = [x for x in sorted(steps(L2)) if x[0] >= t_req - 1]; S['steps_flowk'] = [(round(x - st2[0][0], 2), p, s) for x, p, s in st2] if st2 else []
        check('flow K: centered panel shown', 'progress: centered panel shown' in L2)
        wp = [p for _, p, s in st2 if s == 'Opening your world']
        check('flow K: "Opening your world" with the game percent (50..95)', wp and min(wp) >= 50 and max(wp) <= 95 and len(set(wp)) >= 2, wp)
        check('flow K: steps only go up, end at 100', st2 and all(st2[i][1] <= st2[i + 1][1] for i in range(len(st2) - 1)) and st2[-1][1] == 100, S['steps_flowk'])
        check('three frames saved', all(os.path.exists(os.path.join(OUT, f'shot_{n}.png')) for n in (DESIGN + '_low', DESIGN + '_mid', '3_panel_world')))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
