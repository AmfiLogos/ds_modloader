#!/usr/bin/env python3
"""Speed buttons + square check boxes on one hidden test copy (default W; TICKSPEED_COPY=<K..Z>).

The break it catches: the clock's 4th speed button (very fast) on screen. It has no click (the stock Initialize hides it
and adds no listener; SetSpeedChangeOnUI(3) throws) and VeryFast is clamped to Fast in sessions, so a player sees a
button that does nothing (owner 2026-10-20). The multiplayer clock code showed it in every session.
  1. title: shot (the loader's auto-update switch in the corner), the load list with its filter boxes (off, on): shots
  2. single-player world: ui_view very_fast == 0 (the clock exists), HUD shot
  3. multiplayer session (host, speed-up allowed): very_fast == 0 while fast == 1, HUD shot
Installs THIS worktree's build (dsload.dylib + build/mods/*). Copy hidden (window staged off-screen, alpha guard),
muted, windowed 960 x 540. Output: <out>/ (png, summary.json).
Usage: tickspeed_test.py <out dir>"""
import glob, json, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lrig

X = os.environ.get('TICKSPEED_COPY', 'W'); I = lrig.idir(X)
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.path.join(L, 'build', 'tickspeed_out')))
APP = os.path.join(I, 'DungeonSettlers.app'); MD = os.path.join(APP, 'Contents', 'Resources', 'mods')
SAVE = os.path.join(lrig.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
SHOTS = os.path.join(I, 'shots')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def c(**k): return lrig.send(X, k, timeout=60)


def install():
    import plistlib
    pl = plistlib.load(open(os.path.join(APP, 'Contents', 'Info.plist'), 'rb'))
    if not (pl.get('LSBackgroundOnly') and pl.get('LSUIElement')): raise SystemExit(f'refused: copy {X} is not background-only')
    fw = os.path.join(APP, 'Contents', 'Frameworks')
    shutil.copy2(os.path.join(L, 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    for d in glob.glob(os.path.join(L, 'build', 'mods', '*')):
        if not os.path.isdir(d): continue
        for f in glob.glob(os.path.join(d, '**', '*'), recursive=True):
            if not os.path.isfile(f): continue
            dst = os.path.join(MD, os.path.basename(d), os.path.relpath(f, d)); os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(f, dst + '.tmp'); os.replace(dst + '.tmp', dst)
    ld = os.path.join(fw, 'lang'); os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(L, 'lang', '*.json')): shutil.copy2(f, os.path.join(ld, os.path.basename(f)))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    prof = os.path.join(OUT, 'profile.json'); json.dump({'enabled': ['multiplayer', 'custom-modes'], 'order': ['multiplayer', 'custom-modes']}, open(prof, 'w'))
    os.environ['DSL_PROFILE'] = prof
    cr = os.path.join(OUT, 'crashrep'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')
    os.environ['DSL_CRASHREP_TEST'] = cr


def shot(name):
    os.makedirs(SHOTS, exist_ok=True); p = os.path.join(SHOTS, name + '.png')
    j = c(cmd='ui_capture', path=p); time.sleep(1.5)                 # the frame WITH the UI (screenshot = world only)
    ok = os.path.exists(p)
    if ok: shutil.copy2(p, os.path.join(OUT, name + '.png'))
    log('shot', name, 'ok' if ok else ('MISSING %s' % j))


def owner_playing():
    """the owner's own modded game (launcher play copy or the app), not other lanes' test copies"""
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/|Dungeon Settlers Modded.app'], capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if '/Contents/MacOS/' in l and '/private/' not in l and '/test_env/' not in l and '/scratchpad/' not in l
            and 'zsh -c' not in l and 'bash -c' not in l and 'pgrep' not in l]   # other agents' shells quote the path


def start(mp):
    while owner_playing(): log('the owner plays: waiting 60 s', owner_playing()[0][:120]); time.sleep(60)
    mj = os.path.join(I, 'mp.json')
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, 'mp.json.prev.%d' % time.time()))
    if mp: json.dump({'role': 'host', 'port': lrig.pair_port(X), 'name': 'Host', 'allow_speedup': True, 'allow_pause': True}, open(mj, 'w'))
    lrig.start(X, window=True, fps=30)
    lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)


def world(det):
    work = os.path.join(I, 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'ts.json'); shutil.copy2(SAVE, src)
    log('load', c(cmd='load_save', file=src, det=det))
    t0 = time.time(); v = {}
    while time.time() - t0 < 600:
        try: v = c(cmd='ui_view')
        except Exception: v = {}
        s = c(cmd='status')
        if v.get('very_fast', -1) != -1 and s.get('scene') != 1 and not s.get('busy') and (not det or s.get('det', {}).get('locked')): break
        time.sleep(2)
    time.sleep(6)                                                    # a few frames of the session's clock code
    return c(cmd='ui_view')


def main():
    os.makedirs(OUT, exist_ok=True)
    rig = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
    pair, t0 = '', time.time()
    while not pair and time.time() - t0 < 5400:                   # every pair busy (gates): wait for one, 60 s steps
        pair = (subprocess.run([sys.executable, rig, 'lease', 'tickspeed', '--pid', str(os.getpid())], capture_output=True, text=True).stdout.strip().splitlines() or [''])[-1]
        if not pair.isalpha(): pair = ''; time.sleep(60)
    if not pair: raise SystemExit('no free pair lease: not started')
    install()
    try:
        os.environ['DSML_ROOT'] = os.path.join(OUT, 'root'); os.environ['DSL_UPDATE'] = '0'   # the title corner's switch, no network check
        start(False)
        time.sleep(4); shot('1_title')
        c(cmd='mpm_open_load'); time.sleep(3); shot('2_loadlist_off')
        st = c(cmd='mpm_list', flip=1); time.sleep(2.5); shot('3_loadlist_on')
        c(cmd='mpm_list', close=1); time.sleep(2)
        v = world(0)
        check('single-player: clock found, very-fast button hidden', v.get('very_fast') == 0, v)
        shot('4_sp_hud')
        lrig.stop(X)
        start(True)
        v = world(1)
        check('multiplayer host (speed-up allowed): fast shown, very-fast hidden', v.get('fast') == 1 and v.get('very_fast') == 0, v)
        shot('5_mp_hud')
    finally:
        lrig.stop(X)
        subprocess.run([sys.executable, rig, 'release', pair], capture_output=True)
        mj = os.path.join(I, 'mp.json')
        if os.path.exists(mj): os.replace(mj, os.path.join(OUT, 'mp.json.used'))
        for f in ('dsload.log', 'rig.log', 'player.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
    ok = checks and all(x['ok'] for x in checks)
    json.dump({'checks': checks, 'ok': bool(ok)}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log('ALL PASS' if ok else 'SOME FAILED', f'{sum(x["ok"] for x in checks)}/{len(checks)}')


if __name__ == '__main__':
    main()
