#!/usr/bin/env python3
"""Tutorial progress of the normal game in the modded game (owner 2026-10-20; dsl_saves.c dsl_saves_tutorial_import), on
hidden test copy T (DSL_TEST_COPY): hidden, muted, staged off screen. Own folders only: the "normal game" data folder is
<copy>/gameroot/data (its OutGame.json written here, never the owner's), the modded profile <copy>/data. The world is a
new campaign the game makes in the run (mpm_newcampaign), never an owner save.
Real break caught: an advanced player (every tutorial finished in the normal game) gets the tutorial notices again in the
modded game, because the modded profile kept its own, older OutGame.json.
 A  normal game: no tutorial finished; modded: fresh: nothing is brought. Start a new campaign, play (shots).
    Note: a fresh world shows no tutorial notice by itself (they need player UI actions, quests or later state), so the
    shots alone do not show the difference; the checks are on the modded OutGame.json the game reads.
 B  normal game: every tutorial of AlertTable (ALERT_Tutorial_<id> -> "tutorial.complete.<id>",
    AlertEvaluator.IsTutorialAlreadySeen) finished; modded: fresh again. The start brings the keys; the same world
    (a new campaign again) shows no tutorial notice; the normal game's file is unchanged.
Output: argv[1] (default ~/ds_launcher/loader/test_env/l1_out/tutorial): logs, shots, summary.json."""
import json, os, sys, time, glob, shutil, hashlib
os.environ.setdefault('DSL_TEST_COPY', 'T')
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
if len(sys.argv) < 2: sys.argv.append(os.path.expanduser('~/ds_launcher/loader/test_env/l1_out/tutorial'))
import l1_saves_test as S                                    # copy set-up, start, load, stop (same copy, same OUT)
T, r, I, X = S.T, S.r, S.I, S.X
check, log = T.check, T.log
TUT_IDS = ['AutoSkill', 'CameraMove', 'CameraZoom', 'ConsumptionPolicy', 'DeadUnitTreatment', 'DungeonPortal', 'EnergyStatus',
           'GameObjective', 'GameSpeed', 'GoddessTears', 'Health', 'Hunger', 'Inspect', 'InventoryItemInteraction',
           'InventoryManagement', 'ItemWeight', 'LevelUp', 'MoodStatus', 'MultiSelect', 'Research', 'RoomType', 'SkillReset',
           'SkillsAbility', 'Storage', 'StressStatus', 'TaskPriority', 'Tooltip', 'UnitFire']   # AlertTable ALERT_Tutorial_*


def outgame(completed):
    return {'TutorialSaveData': {'OccurredConditions': [], 'CompletedTutorials': ['tutorial.complete.' + i for i in completed]},
            'ItemObtainedSaveData': {'ObtainedItems': []}, 'CreditRollSaveData': {'PlayedRevisions': []},
            'DifficultyUnlockSaveData': {'IsDownfallUnlocked': False}}


def setup(stock_completed):
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (S.MSAVES, S.GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(I, 'data', 'OutGame.json')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(S.GSAVES); os.makedirs(S.MSAVES)
    json.dump(outgame(stock_completed), open(os.path.join(S.GROOT, 'data', 'OutGame.json'), 'w'), indent=2)
    json.dump(outgame([]), open(os.path.join(I, 'data', 'OutGame.json'), 'w'), indent=2)   # fresh modded profile (never the seed's)


def sha(p): return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def play(tag):
    """a NEW campaign made by the game itself (the new-campaign screen's Start, no tutorial region), the intro clicked
    through: the game's opening tutorial notices (GameObjective after EventFinished_Tutorial0, then CameraMove ...) show
    for a profile that has not finished them"""
    r.send(X, {'cmd': 'mpm_newcampaign', 'name': 'Tut ' + tag, 'seed': '4242', 'scenario': 0, 'tutorial': 0}, timeout=20, connect_wait=0)
    t0 = time.time(); st = {}; intro = 0
    while time.time() - t0 < 180:                              # the narrative intro (pop-up 25) holds the load: click through it
        try:
            if r.send(X, {'cmd': 'mpm_skipintro'}, timeout=10, connect_wait=0).get('open'): intro += 1
            st = T.status()
        except Exception: pass
        if st.get('scene') == 2 and not st.get('busy') and st.get('tick', 0) > 0: break
        if intro == 0 and 20 < time.time() - t0 < 23: T.shot(f'{tag}_loading')
        time.sleep(2)
    check(f'{tag}: new campaign started (intro clicked {intro}x)', st.get('scene') == 2 and st.get('tick', 0) > 0, st)
    clicks = 0
    for k in range(30):                                        # the opening story event: its first choice, page by page
        try: st = r.send(X, {'cmd': 'ui_story', 'action': 'state'}, timeout=10, connect_wait=0)
        except Exception: st = {}
        if not st.get('active'):
            if clicks: break
            time.sleep(2); continue
        r.send(X, {'cmd': 'ui_story', 'action': 'click', 'idx': 0}, timeout=10, connect_wait=0); clicks += 1; time.sleep(2)
    log(tag, 'story event choices clicked', clicks)
    r.send(X, {'cmd': 'set_speed', 'speed': 1}, timeout=10, connect_wait=0)
    for k in range(4):
        time.sleep(15); T.shot(f'{tag}{k}_world_{15 * (k + 1)}s')


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    Sm = {'checks': T.checks, 'violations': T.violations}
    S.install()
    os.makedirs(T.SDIR, exist_ok=True)
    g = S.threading.Thread(target=T.guard, daemon=True); g.start()
    try:
        # ---------------------------------------------------------------- A baseline
        setup([])
        S.start_modded(('multiplayer',), worlds={})
        check('A: nothing to bring (the normal game finished no tutorial)', 'tutorial: the modded profile already has every tutorial key' in T.dlog(),
              [l for l in T.dlog().splitlines() if 'tutorial' in l][:3])
        play('A')
        S.stop_all()
        # ---------------------------------------------------------------- B every tutorial finished in the normal game
        setup(TUT_IDS)
        gfile = os.path.join(S.GROOT, 'data', 'OutGame.json'); g0 = sha(gfile)
        S.start_modded(('multiplayer',), worlds={})
        L = T.dlog()
        check('B: the start brings the normal game\'s finished tutorials', f'tutorial: brought 0 seen hint(s) and {len(TUT_IDS)} finished tutorial(s)' in L,
              [l for l in L.splitlines() if 'tutorial' in l][:3])
        mo = json.load(open(os.path.join(I, 'data', 'OutGame.json')))
        check('B: the modded OutGame.json has every key', sorted(mo['TutorialSaveData']['CompletedTutorials']) == sorted('tutorial.complete.' + i for i in TUT_IDS), mo['TutorialSaveData'])
        play('B')
        mo = json.load(open(os.path.join(I, 'data', 'OutGame.json')))
        check('B: still every key after playing (the game kept them)', set('tutorial.complete.' + i for i in TUT_IDS) <= set(mo['TutorialSaveData']['CompletedTutorials']), mo['TutorialSaveData'])
        check("B: the normal game's OutGame.json is unchanged", sha(gfile) == g0)
        S.stop_all()
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        S.stop_all()
        check("owner's real save folder unchanged (hash before/after)", S.tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        for f in ['dsload.log', 'dsload.prev.log', 'player.log']:
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(S.OUT, f))
        json.dump(Sm, open(os.path.join(S.OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {S.OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
