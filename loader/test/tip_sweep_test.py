#!/usr/bin/env python3
"""Every hover card of the loader's own views (owner 2026-10-25: the Load list's filter card covered the filter panel and
hid its other boxes) on a hidden test copy (DSL_TEST_COPY, default T; one copy at a time; lease first:
~/ds_multiplayer/rig/rig.py lease <lane> --pid <this pid>). Views: the title, the Mods window (Mods and Fixes tabs, a mod's
settings), a world's mods window, the Load list (world rows, filter panel). Each card shown one after the other (dsl_reg.c
tipsweep): it must be inside the screen and cover neither its panel nor a window around it (the kit's placement, dsl_ui.c).
The real break: a card over its own window hides the controls the player is about to click. On the old kit (cards that
did not fit on the right flipped over their own panel, and the kit's move to the left was ignored) this fails at UI 1.4:
the filter cards over the filter panel (16:9 and 16:10), every Mods window card over the Mods window (16:10). When no side
is free (16:10 UI 1.4, a tall card by the Mods window) the card may go 32 px into the window's edge, no more.
Usage: tip_sweep_test.py <out dir>   (env TS_RUNS = comma list of sizes: big,small,wide; default all)"""
import json, os, re, sys, time, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'T')
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/tipsweep'))
import l1_filter_test as F                                          # its copy, saves, worlds, starts (argv[1] = OUT)
T, S, r = F.T, F.S, F.r
check, log = T.check, T.log
UI = os.path.join(OUT, 'ui'); os.makedirs(UI, exist_ok=True)
_root = F.fake_root
F.fake_root = lambda: dict(_root(), DSL_UI_TEST=UI)                # the Mods window's test commands (dsl_win.c)
SEQ = [0]
RX = re.compile(r"reg test: tipsweep (\d+)/(\d+) shown (\d) title '([^']*)' el (\S+) card (\S+) panel (\S+) keep (\S*)")


def ui(op, **kw):
    SEQ[0] += 1; seq = str(SEQ[0]); kw.update({'op': op, 'seq': seq})
    tmp = os.path.join(UI, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 20:
        try:
            j = json.load(open(os.path.join(UI, 'result.json'), encoding='utf-8', errors='replace'))
            if j.get('seq') == seq: return j
        except Exception: pass
        time.sleep(0.1)
    raise RuntimeError('ui command timed out: %s' % op)


def rect(s): return [float(x) for x in s.split(',')]


EDGE = 32                                                           # px; 16:10 UI 1.4: no free side for a tall card by the Mods window


def edge(c, k):
    """how deep the card goes into rect k (the smaller side of their overlap; 0 = apart)"""
    w, h = min(c[2], k[2]) - max(c[0], k[0]), min(c[3], k[3]) - max(c[1], k[1])
    return min(w, h) if w > 0.5 and h > 0.5 else 0


def sweep(tag, view, size):
    """every card of the views open now; returns the number of cards"""
    mark = len(T.dlog()); T.request({'cmd': 'tipsweep'}); t0 = time.time()
    while time.time() - t0 < 90 and 'reg test: tipsweep done' not in T.dlog()[mark:]: time.sleep(0.3)
    lines = RX.findall(T.dlog()[mark:])
    check(f'{tag} {view}: the sweep ran to the end', 'reg test: tipsweep done' in T.dlog()[mark:], len(lines))
    sw, sh = size
    for i, n, on, title, el, card, panel, keep in lines:
        c, p = rect(card), rect(panel); ks = [rect(k) for k in keep.split(';') if k]
        name = f'{tag} {view} "{title}"'
        log(name, 'el', el, 'card', c, 'panel', p, 'keep', ks)
        check(f'{name}: the card shows', on == '1', (on, title))
        check(f'{name}: inside the screen', c[0] >= -0.5 and c[1] >= -0.5 and c[2] <= sw + 0.5 and c[3] <= sh + 0.5, (c, sw, sh))
        bad = [k for k in [p] + ks if edge(c, k) > EDGE]
        check(f'{name}: covers neither its panel nor a window around it (at most a {EDGE} px edge when no side is free)', not bad, (c, bad))
    return len(lines)


def run(size, scale, lang):
    tag = f'{size[0]}x{size[1]} UI {scale} {lang}'
    F.start(size, scale, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(2)
    n = sweep(tag, 'title', size)
    ui('open'); time.sleep(1.5); n += sweep(tag, 'Mods window, Mods tab', size); F.shot(f'sweep_{lang}_mods')
    ui('click', target='tab-mods:fixes'); time.sleep(1); n += sweep(tag, 'Mods window, Fixes tab', size)
    st = ui('state'); names = [c.get('name', '') for c in st.get('controls', [])] if isinstance(st.get('controls'), list) else []
    sets = [x for x in names if x.startswith('settings:')]
    if not sets:
        ui('click', target='tab-mods:game'); time.sleep(1)
        st = ui('state'); names = [c.get('name', '') for c in st.get('controls', [])] if isinstance(st.get('controls'), list) else []
        sets = [x for x in names if x.startswith('settings:')]
    if sets:
        ui('click', target=sets[0]); time.sleep(1); n += sweep(tag, f'mod settings ({sets[0]})', size); ui('click', target='set-back'); time.sleep(0.5)
    ui('close'); time.sleep(1)
    ui('open_world', guid=F.ELVEN_GUID, world='Elven'); time.sleep(1.5); n += sweep(tag, "a world's mods window", size); ui('close'); time.sleep(1)
    F.open_list(); F.lf('lf', multiplayer=1); time.sleep(1); n += sweep(tag, 'Load list', size); F.shot(f'sweep_{lang}_load')
    check(f'{tag}: cards swept', n >= 10, n)
    S.stop_all()


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {F.X} already running: {p}')
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    os.makedirs(T.SDIR, exist_ok=True)
    F.install(); F.seed()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    cfg = {'big': ((960, 540), 1.4, 'de'), 'small': ((960, 600), 1.0, 'en'), 'wide': ((960, 600), 1.4, 'ja')}
    try:
        for w in os.environ.get('TS_RUNS', 'big,small,wide').split(','): run(*cfg[w])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        S.stop_all()
        check("owner's real save folder unchanged (hash before/after)", S.tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"RESULT {sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, out {OUT}")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
