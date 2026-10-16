#!/usr/bin/env python3
"""Mods belong to the WORLD, not to one save file (owner v13 feedback #4), on test copy S (hidden, muted, staged; take a
lease first). Uses this worktree's build/.
 1. worlds.json written by an old build with a per-save key (<Clan>__AutoSave_<hash>_0) -> moved to the world's guid
 2. Load list "Change" -> the Mods window heading names the world ("Mods for Elven Expedition"), not the save file
 3. "Save for this world" (gcd-cdr off) -> the world's set; it survives an Apply (soft restart) at the title
 4. the autosave loads with the world's set (flow K into it); a manual save of that world made in game uses the same set
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, glob, shutil, signal, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/world')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
MANUAL = 'WorldModsManual'


def worlds():
    return json.load(open(os.path.join(T.MODS, 'worlds.json'))).get('worlds', {})


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    for f in glob.glob(os.path.join(T.I, 'data', 'Saves', MANUAL + '*')):
        os.makedirs(os.path.join(T.I, 'old'), exist_ok=True); os.replace(f, os.path.join(T.I, 'old', os.path.basename(f) + '.%d' % time.time()))
    V.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        guid = T.GUID; T.GUID = T.SAVE                       # start_a writes {T.GUID: set}: an old per-save entry
        a = T.start_a(extra_env={'DSL_UI_TEST': V.UI}); T.GUID = guid
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
        w = worlds(); S['worlds_after_start'] = w
        check('per-save entry moved to the world', w.get(guid) == 'gcd-cdr' and T.SAVE not in w, w)
        # 2 heading
        r.send(T.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', T.wait_log('rows: Load list rebuilt', 20)); time.sleep(1.5)
        T.request({'cmd': 'change', 'row': 0}); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        hd = [c['text'] for c in st.get('controls', []) if c['name'] == 'heading']
        S['heading'] = hd
        check('heading names the world, not a save file', hd == ['Mods for Elven Expedition'], hd)
        T.shot('2_world_mods')
        # 3 the world's set
        V.ui('click', target='mod:gcd-cdr'); V.ui('click', target='apply'); time.sleep(1)
        check('Save for this world -> world set without gcd-cdr', worlds().get(guid) == 'vanilla', worlds())
        V.ui('close'); time.sleep(1)
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']})
        dt, b = T.wait_swap(a, t_req, 1)
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 60); time.sleep(2)
        check('B runs gcd-cdr', 'reg: running gameplay set gcd-cdr' in T.dlog())
        check("the world's set survives the Apply", worlds().get(guid) == 'vanilla', worlds())
        # 4 autosave with the world's set, then a manual save of that world
        mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0)
        check('autosave refused in B (world set differs)', V.wait_line(r'world check: REFUSED', mark, 30) is not None)
        time.sleep(1.5); T.clear_flags(); t_req = time.time(); T.request({'cmd': 'k_ok'})
        dk, c2 = T.wait_swap(b, t_req, 2)
        T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), 'C in the world', 90); time.sleep(3)
        L = T.dlog(); check('autosave loads with the world set', 'reg: running gameplay set vanilla' in L and 'uses this mod set: allowed' in L)
        T.request({'cmd': 'save_file', 'name': MANUAL})
        t0 = time.time()
        while time.time() - t0 < 30 and not glob.glob(os.path.join(T.I, 'data', 'Saves', MANUAL + '*.json')): time.sleep(0.5)
        files = [os.path.basename(f) for f in glob.glob(os.path.join(T.I, 'data', 'Saves', MANUAL + '*.json'))]
        S['manual_files'] = files
        check('manual save written', files, files); time.sleep(2)
        name = files[0][:-5] if files else MANUAL
        mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': name}, timeout=20, connect_wait=0)
        m = V.wait_line(r'world check: %s \(world ([0-9a-f-]+)\) uses this mod set: allowed' % name, mark, 30)
        check('manual save = same world, same set', m is not None and m.group(1) == guid, V.since(mark)[-300:])
        T.shot('4_manual_loaded')
        check('no per-save keys in worlds.json', set(worlds()) == {guid} or all('-' in k for k in worlds()), worlds())
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
        for f in ['dsload.log', 'dsload.prev.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
