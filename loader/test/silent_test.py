#!/usr/bin/env python3
"""Test copies are silent (loader src/dsl_soft.c silent_*; owner 2026-10-25: "can you make sure the background tests don't
have sound please?"). One hidden test copy (default X; DSL_TEST_COPY), one rig lease, WITHOUT the multiplayer module (its
own rig mute would hide a loader fault): sound-probe (test/sound_probe.c) + sound-fix. Never the owner's game, saves or prefs.

  silent_test.py [out]

The real break it catches: a test copy plays sound (AudioListener.volume above 0) at the title, after a Mute / slider
change in the Options window, or after a soft restart ("audio back"); or the mute changes the mixer the Sound settings
write (tests that read the mixer would break). AudioListener is read in the copy (<DSRIG_DIR>/listener_req.txt -> the
loader's log line "test copy: listener volume ...")."""
import json, os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'V')
import lrig as r
import sound_fix_test as sf                        # probe / state / open_window on the same copy (sf.X)
X, I, LB, H = sf.X, sf.I, sf.LB, sf.H
checks = []
sf.checks = checks                                 # sf.slider_case's checks count here too


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def listener(limit=15):
    """AudioListener.volume in the copy now (None = no answer)"""
    mark = len(sf.dlog())
    p = os.path.join(I, 'listener_req.txt'); open(p, 'w').write('1\n')
    t0 = time.time()
    while time.time() - t0 < limit:
        m = re.search(r'test copy: listener volume (-?[\d.]+)', sf.dlog()[mark:])
        if m: return float(m.group(1))
        time.sleep(0.3)
    return None


def up(out):
    if r.alive(r.read_pid(X)): raise SystemExit(f'copy {X} already running')
    md = os.path.join(out, 'mods'); os.makedirs(md)
    rel = os.path.join(LB, 'build', 'mods_all', 'mac', 'sound-fix')
    shutil.copytree(rel if os.path.isdir(rel) else os.path.join(LB, 'build', 'mods', 'sound-fix'), os.path.join(md, 'sound-fix'))
    pd = os.path.join(md, 'sound-probe'); os.makedirs(pd)
    subprocess.run(['clang', '-arch', 'arm64', '-O2', '-Wall', '-I', os.path.join(LB, 'sdk'), '-dynamiclib', '-install_name', '@rpath/sound-probe.dylib',
                    '-o', os.path.join(pd, 'sound-probe.dylib'), os.path.join(HERE, 'sound_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(pd, 'sound-probe.dylib')], capture_output=True)
    json.dump({'id': 'sound-probe', 'name': 'sound probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
               'game_builds': ['25846389'], 'platforms': {'mac-arm64': {'entry': 'sound-probe.dylib'}}}, open(os.path.join(pd, 'mod.json'), 'w'), indent=1)
    en = ['sound-probe', 'sound-fix']
    prof = os.path.join(out, 'profile.json'); json.dump({'enabled': en, 'order': en}, open(prof, 'w'))
    for p in ('dsload.log', 'player.log', 'sound_probe_req.txt', 'listener_req.txt', 'softrestart'):
        sf.aside(os.path.join(I, p), out)
    r.prepare_data(X)
    # without MP, persistentDataPath is the copy's own home folder: the same windowed 960x540 settings (uikit_test.py)
    hd = os.path.join(I, f'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(hd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(hd, f))
    g = json.load(open(os.path.join(hd, 'UserSetting.json')))['GraphicSettingData']
    assert g['SaveCurrentScreenType'] == 1 and g['ResolutionWidth'] == 960, g
    cr = os.path.join(out, 'crash'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data'),
                       'DSL_CLOUD': '0', 'DSL_NO_ALERT': '1', 'DSL_UPDATE': '0', 'DSL_SOFT_TEST': '1', 'DSL_CRASHREP_TEST': cr,
                       'DSL_SOFT_STAGE': '-20000,-20000'})        # B's window off every display at the swap (soft_test.py)
    os.environ.pop('DSML_SESSION', None)
    r.start(X, window=True)                            # hidden window (soft restart B needs one; soft_test.py)
    t0 = time.time()                                   # no rig socket without MP: the title = the game's mixer is readable
    while time.time() - t0 < 600:
        if 'sound-probe: ready' in sf.dlog() and sf.state()['game']: break
        if not r.alive(r.read_pid(X)): raise SystemExit('the copy ended before the title')
        time.sleep(3)
    time.sleep(3)
    return prof


def soft_restart(prof):
    """flow J from the title (test request file): B starts hidden, takes over, A quits"""
    sd = os.path.join(I, 'softrestart'); a_pid = r.read_pid(X)
    tmp = os.path.join(sd, 'apply_request.json.tmp'); open(tmp, 'w').write(json.dumps({'profile': prof})); os.replace(tmp, os.path.join(sd, 'apply_request.json'))
    t0 = time.time()
    while time.time() - t0 < 180 and not os.path.exists(os.path.join(sd, 'handoff.json.shown')):
        if os.path.exists(os.path.join(sd, 'handoff.json.failed')): return False
        time.sleep(0.3)
    while r.alive(a_pid) and time.time() - t0 < 210: time.sleep(0.3)
    time.sleep(3)
    exe = os.path.join(I, r.EXE_REL)
    b = [int(p) for p in subprocess.run(['pgrep', '-f', exe], capture_output=True, text=True).stdout.split() if int(p) != a_pid]
    if len(b) == 1: open(r.pidfile(X), 'w').write(f'{b[0]}\n')     # B is now the copy: the end of the run stops it
    log('soft restart: A', a_pid, 'B', b)
    return os.path.exists(os.path.join(sd, 'handoff.json.shown')) and not r.alive(a_pid) and len(b) == 1


def flow(prof):
    L = sf.dlog()
    check('start: the loader calls this copy a test copy and mutes it', re.search(r'test copy: audio muted \(AudioListener 0; (background-only app|test control channel)', L),
          [l for l in L.splitlines() if 'test copy' in l or 'audio:' in l][-6:])
    check('start: the setter hook is in', 'AudioListener held at 0: setter hook ok' in L, [l for l in L.splitlines() if 'held at 0' in l])
    check('start: CoreAudio process mute on', 'process inaudible in CoreAudio' in L, [l for l in L.splitlines() if 'audio muted' in l])
    v = listener(); check(f'title: AudioListener.volume 0 ({v})', v == 0.0)
    S1 = sf.open_window()
    check('Options window open', S1['window'] is not None, S1['raw'])
    S = sf.slider_case('title', 1, 37, True, S1)
    v = listener(); check(f'after a slider change: AudioListener.volume 0 ({v})', v == 0.0)
    log('mute', sf.probe('mute 1 1', 'mute')); time.sleep(1.5); S = sf.state()
    check('Mute BGM on: the mixer still follows the settings (BGM -80 dB)', sf.near(S['game'].get('BGM'), -80.0), S['raw'])
    v = listener(); check(f'after Mute on: AudioListener.volume 0 ({v})', v == 0.0)
    log('mute', sf.probe('mute 1 0', 'mute')); time.sleep(1.5); S = sf.state()
    check('Mute BGM off: the mixer still follows the settings (BGM back to the slider value)',
          sf.near(S['game'].get('BGM'), S['stored'].get('BGM')) and S['game'].get('BGM', -80) > -80, S['raw'])
    v = listener(); check(f'after Mute off: AudioListener.volume 0 ({v})', v == 0.0)
    ok = soft_restart(prof)
    check('soft restart: B took over, A ended', ok)
    B = sf.dlog()
    check('soft restart: B is a test copy too, "audio back" keeps 0', 'test copy: audio muted (AudioListener 0' in B and 'test copy, AudioListener stays 0' in B,
          [l for l in B.splitlines() if 'audio' in l][-6:])
    v = listener(); check(f'after the soft restart: AudioListener.volume 0 ({v})', v == 0.0)
    S = sf.state()
    check('after the soft restart: the mixer has the stored volumes (BGM)', sf.near(S['game'].get('BGM'), S['stored'].get('BGM')), S['raw'])
    log('evidence: the game asked for sound and was kept at 0:', [l for l in (L + B).splitlines() if 'by the game: kept 0' in l][:4])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(LB, 'test_out', 'silent_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    lane = 'silent-%d' % os.getpid(); pair = None; t0 = time.time()
    while time.time() - t0 < 1800 and not pair:
        p = subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        pair = p.stdout.strip() if p.returncode == 0 else None
        if not pair: time.sleep(20)
    if not pair: raise SystemExit('no rig lease in 30 min')
    mine = None                                        # only a copy this run started is ever stopped (another lane's run uses the letter too)
    try:
        if r.alive(r.read_pid(X)): raise SystemExit(f'copy {X} is in use (pid {r.read_pid(X)}): DSL_TEST_COPY=<another letter>')
        prof = up(out); mine = r.read_pid(X)
        flow(prof)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for f in ('dsload.log', 'dsload.prev.log', 'player.log', 'guard.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(out, f))
        if mine is not None: sf.down()                 # A, or B after the soft restart (soft_restart() wrote its pid)
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    n = sum(c['ok'] for c in checks)
    json.dump(checks, open(os.path.join(out, 'checks.json'), 'w'), indent=1)
    print(f'RESULT silent_test: {n}/{len(checks)} PASS ({out})')
    sys.exit(0 if n == len(checks) else 1)


if __name__ == '__main__':
    main()
