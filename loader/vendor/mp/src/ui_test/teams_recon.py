#!/usr/bin/env python3
"""Recon for the Teams window rework: open stock popups on one hidden (rendered) copy, dump their hierarchy
(names, sizes, sprites, TMP fonts) into rig.log and capture the UI as PNG (build/teams_test/shots/)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, tm = T.rig, T.log, T.c, T.st, T.wait, T.tm
SHOTS = os.path.expanduser('~/ds_multiplayer/build/teams_test/shots')
save = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
rig.DYLIB = os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib')
pair = rig.lease('teams', os.getpid()); X = pair[0]
try:
    json.dump({'role': 'host', 'port': rig.PAIR_PORTS.get(pair, 7795), 'name': 'The owner'}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    rig.start(X, fps=10, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
    c(X, cmd='load_save', file=src, det=1)
    wait('locked', lambda: st(X).get('det', {}).get('locked'))
    time.sleep(6)
    log('shot base', tm(X, action='shot', path=f'{SHOTS}/recon_base.png')['rc'])
    for ty, cls in ((13, 'TaskPriorityView'), (12, 'ConsumePolicyView'), (8, 'RecruitView')):
        log('popup', ty, tm(X, action='popup', type=ty)['rc']); time.sleep(3)
        tm(X, action='dump', cls=cls, depth=5)
        log('shot', cls, tm(X, action='shot', path=f'{SHOTS}/recon_{cls}.png')['rc'])
        c(X, cmd='ord_inject', ev='ClosePopUpRequested', **{'i32@16': ty}); time.sleep(2)
    tm(X, action='open'); time.sleep(2)
    log('shot teams', tm(X, action='shot', path=f'{SHOTS}/recon_teams_now.png')['rc'])
finally:
    for i in pair:
        try: rig.stop(i)
        except Exception as e: log('stop', i, e)
    rig.release(pair)
