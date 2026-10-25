#!/usr/bin/env python3
"""Remove File on the player's path (bug 2026-10-20, a Windows player: "Remove File" in Multiplayer > Host a world did not
delete the save). The save's remove is asked once more for a game save ("also used by Dungeon Settlers without mods").
The game's IL2CPP build inlines SaveLoadView.ConfirmRemove into the stock box's Yes callback, so a hook on ConfirmRemove
never ran on the player's path: this test selects the save and presses Remove File (SaveLoadView.SelectSlot +
OnClickRemove), then clicks the game's own box (CommonNoticeView.OnClick_Confirm / OnClick_Cancle), in both lists:
 L  Load Campaign;  H  Multiplayer > Host > Load save ("Host a world", single-player worlds shown with its box).
Per list: stock Cancel = nothing deleted, no question; game save + Yes = the question shows; its Cancel = the game file
stays; its Delete = the game file is gone; a modded-only save + Yes = deleted, no question.
Test copy O (hidden, muted; lease first). Own save folders only (<copy>/gameroot/data/Saves = the "game" folder,
<copy>/data/Saves = modded); the owner's saves and prefs are hashed before/after.
Env: DSL_TEST_MODS = built mods (this worktree's loader/build/mods). Output: argv[1] (logs, shots, summary.json)."""
import json, os, sys, time, shutil, threading, subprocess, signal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_MODS', os.path.join(os.path.dirname(HERE), 'build', 'mods'))
import l1_saves_test as S
T, r = S.T, S.r
check, log = T.check, T.log
X, I = S.X, S.I
GSAVES, MSAVES, ELVEN, MOON, SEED = S.GSAVES, S.MSAVES, S.ELVEN, S.MOON, S.SEED
GFILE, MFILE = os.path.join(GSAVES, ELVEN + '.json'), os.path.join(MSAVES, MOON + '.json')


def seed():
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (MSAVES, S.GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(T.MODS, 'save_copies'), os.path.join(I, 'mp_tags.txt')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), GSAVES)
    shutil.copy2(os.path.join(SEED, MOON + '.json'), MSAVES)


def mp(cmd, **kw):
    return r.send(X, dict(cmd=cmd, **kw), timeout=20, connect_wait=0)


def notices(mark):
    return [l for l in T.dlog()[mark:].splitlines() if 'ui: notice' in l or 'saves: delete' in l]


def remove(name, stock, mine=None, tag=''):
    """press Remove File on that save; stock = answer to the game's own box; mine = answer to the loader's question
    (None = it must not show). Returns the log lines since."""
    mark = len(T.dlog())
    S.sreq({'cmd': 'del_save_ui', 'name': name}); time.sleep(2.5)
    T.shot(f'{tag}_stock_box')
    b = mp('mpm_box', action=stock)
    check(f'{tag}: the game\'s own box is up after Remove File', b.get('ok'), b)
    if mine is None:
        time.sleep(4)
        return T.dlog()[mark:]
    shown = S.wait_line('saves: delete question shown for save ' + name, mark, 15)
    time.sleep(1.5); T.shot(f'{tag}_question')
    check(f'{tag}: the question "also used by Dungeon Settlers without mods" shows', shown, notices(mark)[-6:])
    st = mp('mpm_box', action=mine)
    check(f'{tag}: the question is on screen and answerable (its button clicked)', st.get('ok'), st)
    time.sleep(4)
    return T.dlog()[mark:]


def gone(p, limit=15):
    t0 = time.time()
    while time.time() - t0 < limit and os.path.exists(p): time.sleep(0.5)
    return not os.path.exists(p)


def one_list(tag):
    L = remove(ELVEN, 'cancel', None, tag + '1')
    check(f'{tag}: Cancel in the game\'s box = nothing deleted, no question', os.path.exists(GFILE) and os.path.exists(MFILE)
          and 'delete question shown' not in L, (sorted(os.listdir(GSAVES)), sorted(os.listdir(MSAVES))))
    L = remove(ELVEN, 'confirm', 'cancel', tag + '2')
    check(f'{tag}: game save, question answered Cancel = the game file stays', os.path.exists(GFILE) and 'answered Cancel' in L,
          (sorted(os.listdir(GSAVES)), [l for l in L.splitlines() if 'saves' in l][-4:]))
    L = remove(ELVEN, 'confirm', 'confirm', tag + '3')
    check(f'{tag}: game save, question answered Delete = the game file is gone', gone(GFILE) and os.path.exists(MFILE),
          (sorted(os.listdir(GSAVES)), [l for l in L.splitlines() if 'saves' in l][-4:]))
    L = remove(MOON, 'confirm', None, tag + '4')
    check(f'{tag}: modded-only save + Yes = deleted, no question', gone(MFILE) and 'delete question shown' not in L,
          (sorted(os.listdir(MSAVES)), [l for l in L.splitlines() if 'saves' in l][-4:]))
    T.shot(f'{tag}5_list_after')


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    os.makedirs(T.SDIR, exist_ok=True)
    S.install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    try:
        # ---------------------------------------------------------------- L: Load Campaign
        seed(); S.start_modded(('multiplayer', 'gcd-cdr'), worlds={})
        T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        mp('mpm_open_load'); time.sleep(3)
        check('L: Load list open with both saves', T.wait_log('groups: 1 game, 1 modded', 20), T.dlog()[-300:])
        one_list('L')
        S.stop_all()
        # ---------------------------------------------------------------- H: Multiplayer > Host a world
        seed(); S.start_modded(('multiplayer', 'gcd-cdr'), worlds={})
        T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        for b in (1, 2, 7):                                    # MPB_MULTIPLAYER, MPB_HOST, MPB_LOAD_SAVE (mpmenu.h)
            mp('mpm_press', button=b); time.sleep(2)
        st = mp('mpm_list'); time.sleep(1)
        if st.get('mode') == 1 and not st.get('other'): mp('mpm_list', flip=1); time.sleep(2)   # show single-player worlds
        st = mp('mpm_list')
        # Multiplayer 0.1.27 (UX M4): a host with no multiplayer world gets every world (mode -1 = MPM_LIST_ALL, no toggle);
        # with one, the multiplayer list (mode 1) and the toggle that shows the single-player worlds
        check('H: Host a world open (single-player worlds shown: all worlds, or the multiplayer list + its toggle)',
              st.get('open') and ((st.get('mode') == -1 and st.get('shown', 0) >= 2) or (st.get('mode') == 1 and st.get('other'))), st)
        T.shot('H0_host_list')
        one_list('H')
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        S.stop_all()
        check("owner's real save folder unchanged (hash before/after)", S.tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        for f in ['dsload.log', 'dsload.prev.log', 'rig.log']:
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(S.OUT, f))
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(S.OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {S.OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
