#!/usr/bin/env python3
"""Fast boot lane, phase 1 (measure only): hidden-window boot of test copy P, process start -> title ready, then
optionally a world load. Never the owner's game, saves, prefs or screen.
  fastboot_measure.py <out> [--sample S] [--world] [--fps N] [--set multiplayer,custom-modes,gcd-cdr]
Env for the copy: LRIG_DYLIB = this worktree's build/dsload.dylib (DSL_BOOTPROF=1 logs the game's boot steps in A),
DSL_MODS_DIR = the main checkout's build/mods (read only)."""
import json, os, shutil, subprocess, sys, time, threading
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'P'; I = r.idir(X)
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(sys.argv[1]); os.makedirs(OUT, exist_ok=True)
a = sys.argv[2:]
SAMPLE = float(a[a.index('--sample') + 1]) if '--sample' in a else 0
PHASES = [('splash', 'prof: first tick', 3), ('sheets', 'MainFlow.Awake', 1), ('font', 'LoadFont>d__4 state 0 -> 1', 7),
          ('prefabs', 'LoadTiles>d__7 state 0 -> 1', 2), ('title', 'MainFlow.Start state 5 -> -1', 2)] if '--phases' in a else \
         [('title', 'MainFlow.Start state 5 -> -1', 2)] if '--title' in a else []
WORLD = '--world' in a
FPS = int(a[a.index('--fps') + 1]) if '--fps' in a else 60
SET = (a[a.index('--set') + 1] if '--set' in a else 'multiplayer,custom-modes,gcd-cdr').split(',')
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
log = lambda *x: print(time.strftime('%H:%M:%S'), *x, flush=True)

prof = os.path.join(OUT, 'profile.json')
json.dump({'enabled': [s for s in SET if s], 'order': [s for s in SET if s]}, open(prof, 'w'))
pair = None; t_l = time.time()
while not pair and time.time() - t_l < 1800:
    p = subprocess.run([sys.executable, RIG, 'lease', 'fastboot', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = p.stdout.strip() if p.returncode == 0 else None
    if not pair: time.sleep(15)
log('lease', pair)
res = {'fps': FPS, 'set': SET}
try:
    if not pair: raise SystemExit('no lease')
    for f in ('dsload.log', 'player.log'):
        if os.path.exists(os.path.join(I, f)): os.replace(os.path.join(I, f), os.path.join(OUT, 'prev_' + f))
    mj = os.path.join(I, 'mp.json')
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, 'mp.json.aside'))
    os.environ.update({'LRIG_DYLIB': os.path.join(HERE, '..', '..', 'build', 'dsload.dylib'),
                       'DSL_MODS_DIR': os.path.expanduser('~/ds_launcher/loader/build/mods'), 'DSL_PROFILE': prof,
                       'DSL_BOOTPROF': '1'})
    os.environ.pop('DSML_SESSION', None)
    t0 = time.time()
    pid = r.start(X, window=True, fps=FPS)
    res['pid'] = pid
    if SAMPLE:
        threading.Thread(target=lambda: subprocess.run(['sample', str(pid), str(int(SAMPLE)), '1', '-mayDie', '-file',
                                                        os.path.join(OUT, 'sample_boot.txt')], capture_output=True), daemon=True).start()
    if PHASES:                                            # one sample per boot phase, started when its log line shows up
        def phased():
            todo = list(PHASES); lp = os.path.join(I, 'dsload.log')
            while todo and time.time() - t0 < 120:
                L = open(lp, errors='replace').read() if os.path.exists(lp) else ''
                if todo[0][1] in L:
                    name, _, dur = todo.pop(0); log('sample', name, dur, 's')
                    subprocess.run(['sample', str(pid), str(dur), '1', '-mayDie', '-file', os.path.join(OUT, f'sample_{name}.txt')], capture_output=True)
                else: time.sleep(0.02)
        threading.Thread(target=phased, daemon=True).start()
    seen = []; last = None; title = None
    while time.time() - t0 < 300:
        try:
            s = r.send(X, {'cmd': 'status'}, timeout=10, connect_wait=0)
        except Exception:
            time.sleep(0.1); continue
        k = (s.get('scene'), s.get('busy'))
        if k != last:
            last = k; seen.append((round(time.time() - t0, 3), k)); log(seen[-1])
        if s.get('scene') == 1 and not s.get('busy'):
            title = time.time() - t0; break
        time.sleep(0.1)
    res['title_s'] = title; res['status_seen'] = seen
    log('title ready after', title)
    if WORLD and title:
        time.sleep(3)
        work = os.path.join(OUT, 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(r.SAVE)); shutil.copy2(r.SAVE, src)
        if SAMPLE or '--title' in a:
            threading.Thread(target=lambda: subprocess.run(['sample', str(pid), str(int(SAMPLE or 5)), '1', '-mayDie', '-file',
                                                            os.path.join(OUT, 'sample_world.txt')], capture_output=True), daemon=True).start()
        t1 = time.time(); log('load', r.send(X, {'cmd': 'load_save', 'file': src}))
        st = r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=400, every=0.1)
        res['world_s'] = time.time() - t1; log('world ready after', res['world_s'])
        time.sleep(2)
finally:
    try: r.stop(X)
    except Exception as e: log('stop:', e)
    for f in ('dsload.log', 'player.log'):
        if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
    if pair: subprocess.run([sys.executable, RIG, 'release', pair, 'fastboot'], capture_output=True)
    json.dump(res, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
