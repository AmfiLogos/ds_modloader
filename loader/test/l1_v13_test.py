#!/usr/bin/env python3
"""v13 lane loaderui checks on test copy S (hidden, muted, staged far off every display; never the owner's game).
Take a lease first (rig.py lease loaderui --pid <this pid>). Uses the loader + modules of THIS worktree's build/.
 1. title: "Mods" right below "Multiplayer", no hover card on "Mods"
 2. Mods window: a hover card sits outside the window (right side, or left when no room)
 3. Load list: world row line "Mods: ..." on the right half, hover card lists every mod, right of the list panel
 4. prewarm: switch gcd-cdr on in the Mods window -> hidden game starts 1.5 s later -> Apply swaps (time printed)
 5. flow K at the title (world registered with another set) -> "Load it with them?" -> Load -> C in the world
 6. in the world: Mods window read-only ("Go back to the title screen..."), Apply refused, no new process
 7. in the world: loading a world that uses other mods is refused with the title notice (no "Load it with them?")
Output: <out>/ (logs, shots, summary.json)."""
import json, os, re, shutil, signal, subprocess, sys, time, glob, threading
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import l1_test as T
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/v13')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT
BUILD = os.path.join(os.path.dirname(HERE), 'build')
UI = os.path.join(OUT, 'ui'); os.makedirs(UI, exist_ok=True)
log, check = T.log, T.check


def install():
    app = os.path.join(T.I, 'DungeonSettlers.app')
    T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    for d in glob.glob(os.path.join(BUILD, 'mods', '*')):
        if not os.path.isdir(d): continue
        dst = os.path.join(md, os.path.basename(d))
        for f in glob.glob(os.path.join(d, '**', '*'), recursive=True):
            if os.path.isfile(f):
                t = os.path.join(dst, os.path.relpath(f, d)); os.makedirs(os.path.dirname(t), exist_ok=True)
                shutil.copy2(f, t + '.tmp'); os.replace(t + '.tmp', t)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    log('installed', sorted(os.listdir(md)))


SEQ = [0]
def ui(op, **kw):
    SEQ[0] += 1; seq = str(SEQ[0]); kw.update({'op': op, 'seq': seq})
    tmp = os.path.join(UI, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 20:
        try:
            # the window's test registry keeps 160 bytes of each text: a Russian / Japanese text can end mid-character
            j = json.load(open(os.path.join(UI, 'result.json'), encoding='utf-8', errors='replace'))
            if j.get('seq') == seq: return j
        except Exception: pass
        time.sleep(0.1)
    raise RuntimeError('ui command timed out: %s' % op)


def since(mark):
    return T.dlog()[mark:]


def logs_all():                                                   # B moves A's dsload.log to dsload.prev.log
    t = ''
    for f in ('dsload.prev.log', 'dsload.log'):
        try: t += open(os.path.join(T.I, f), errors='replace').read()
        except OSError: pass
    return t


def wait_count(s, n0, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        if logs_all().count(s) > n0: return True
        time.sleep(0.2)
    return False


def wait_line(rx, mark, limit=30):
    t0 = time.time()
    while time.time() - t0 < limit:
        m = re.search(rx, since(mark))
        if m: return m
        time.sleep(0.2)
    return None


def tip(what, name):
    mark = len(T.dlog()); T.request({'cmd': 'hover', 'what': what})
    m = wait_line(r"reg test: tip %s shown (\d) title '([^']*)' card ([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) panel ([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)" % re.escape(what), mark, 15)
    tl = re.search(r'reg test: hover %s: target (\w+)(?:, text: (.*))?' % re.escape(what), since(mark))
    if m: T.shot(name)
    return m, (tl.group(2) if tl else None)


def outside(m):
    c = [float(m.group(i)) for i in range(3, 7)]; p = [float(m.group(i)) for i in range(7, 11)]
    return (c[0] >= p[2] - 1 or c[2] <= p[0] + 1), c, p


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        a = T.start_a(extra_env={'DSL_UI_TEST': UI})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        # 1 title
        mark = len(T.dlog()); T.request({'cmd': 'title'})
        m = wait_line(r'reg test: title (.*)', mark, 10)
        items = [x.split('|') for x in (m.group(1).split(';') if m else [])]
        labels = [x[0] for x in items if x[1] == '1']
        S['title'] = labels
        check('title: Mods right below Multiplayer', 'Multiplayer' in labels and 'Mods' in labels and labels.index('Mods') == labels.index('Multiplayer') + 1, labels)
        mt, _ = tip('title:Mods', '1_title_mods_hover')
        check('title: no hover card on Mods', mt is not None and mt.group(1) == '0', mt.group(0) if mt else 'no tip line')
        T.shot('1_title')
        # 2 Mods window card
        ui('open'); time.sleep(1.5)
        mw, _ = tip('win:apply', '2_modswin_hover')
        ok, c, p = outside(mw) if mw else (False, None, None)
        check('Mods window: hover card outside the window', mw and mw.group(1) == '1' and ok, mw.group(0) if mw else 'no tip line')
        S['modswin_card'] = {'card': c, 'panel': p}
        ui('close'); time.sleep(1)
        # 3 Load list row
        r.send(T.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', T.wait_log('rows: Load list rebuilt', 20))
        time.sleep(1.5)
        mr, text = tip('row:0', '3_row_hover')
        ok, c, p = outside(mr) if mr else (False, None, None)
        S['row'] = {'text': text, 'card': c, 'panel': p}
        check('world row: mods line on the right with the short name', text and 'Mods:' in text and 'Global cooldown' in text, text)
        check('world row: card "Mods of this world" right of the list panel', mr and mr.group(1) == '1' and mr.group(2) == 'Mods of this world' and c and c[0] >= p[2] - 1, mr.group(0) if mr else '')
        T.shot('3_loadlist')
        # 4 prewarm through the Mods window
        T.clear_flags()
        ui('open'); time.sleep(1.5)
        n1, n2 = logs_all().count('reg: prewarm: a hidden game starts'), logs_all().count('prewarm B ready and waiting')
        ui('click', target='tab-mods:fixes'); time.sleep(1)       # gcd-cdr is a fix: its own tab (owner 2026-10-24)
        t_ch = time.time(); ui('click', target='mod:gcd-cdr')
        check('prewarm starts 1.5 s after the change', wait_count('reg: prewarm: a hidden game starts', n1, 10))
        S['prewarm_start_after_change'] = round(time.time() - t_ch, 2)
        check('prewarm B ready (hidden)', wait_count('prewarm B ready and waiting', n2, 120))
        time.sleep(1)
        t_req = time.time(); ui('click', target='apply')
        dt, b = T.wait_swap(a, t_req, 1)
        S['prewarm_apply_seconds'] = round(dt, 2)
        used = ''.join(open(f, errors='replace').read() for f in glob.glob(os.path.join(OUT, 'round1', 'dsload*.log')))
        check('the prewarmed game was used', 'the prewarmed B pid' in used, [l for l in used.splitlines() if 'prewarm' in l][-3:])
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 60); time.sleep(3)
        L = T.dlog(); check('B runs gcd-cdr', 'reg: running gameplay set gcd-cdr' in L)
        try: ui('close')
        except Exception: pass
        # 5 flow K: the world now registered with no gameplay mod (vanilla) -> B (gcd-cdr) refuses -> Load
        T.request({'cmd': 'world_set', 'guid': T.GUID, 'ids': []}); time.sleep(1)
        mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0)
        check('flow K: title load refused, question asked', wait_line(r'world check: REFUSED', mark, 30) is not None and wait_line(r'This world uses: none\.\\nLoad it with them\?|This world uses: none', mark, 10) is not None)
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'k_ok'})
        dk, c2 = T.wait_swap(b, t_req, 2)
        S['flowK_seconds'] = round(dk, 2)
        T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), 'C in the world', 90); time.sleep(3)
        L = T.dlog(); check('flow K: C in the world without gcd-cdr', 'reg: running gameplay set vanilla' in L and 'uses this mod set: allowed' in L)
        # 6 in the world: read-only window, Apply refused
        ui('open'); st = {}
        for _ in range(20):
            time.sleep(1); st = ui('state')
            if st.get('open'): break
        status = [c for c in st.get('controls', []) if c['name'] == 'status']
        check('world: Mods window says go back to the title', status and 'Go back to the title screen to change mods.' in status[0]['text'], status)
        T.shot('6_world_modswin'); ui('close')
        mark = len(T.dlog()); pids0 = set(T.game_pids())
        T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']}); time.sleep(4)
        check('world: Apply refused', 'reg: Apply refused: not at the title' in since(mark), since(mark)[-300:])
        check('world: no new game process', set(T.game_pids()) == pids0, (pids0, T.game_pids()))
        # 7 in the world: load a world that uses other mods -> refused with the title notice
        T.request({'cmd': 'world_set', 'guid': T.GUID, 'ids': ['gcd-cdr']}); time.sleep(1)
        mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0); time.sleep(4)
        L = since(mark)
        check('world: load with other mods refused, no "Load it with them?"', 'world check: REFUSED' in L and 'This world uses:' not in L, L[-400:])
        T.shot('7_world_refused')
        check('world: still in the world, no new process', set(T.game_pids()) == pids0 and T.status().get('scene') == 2)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        # no game process of this test ever came to the front or showed a window (before or after a swap)
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)   # any FAIL, guard violation or no check: exit 1
