#!/usr/bin/env python3
"""scan_saves.py <dir|zip>...: no player data in what ships (owner 2026-10-16: "make sure that our saves are not packaged
with the modloader as well lol or multiplayer mod"). Every file (zips opened) is checked by its path and, for .json
files, by its content. Exit 1 = a hit (printed).
  path: a folder Saves / CorruptSaves / cloud_backups / save_copies / mp_owners / profiles / instances / test_env, or a
        file named like the game's or the loader's player files (SaveHeaderCache.json, OutGame.json, UserSetting.json,
        UserCampaignLog.json, worlds.json, active.json, pending.json, groups.json, cloud_state.tsv, mp_tags.txt,
        mp_worlds.txt, player.log, rig.log, dsload.log)
  content: a .json with the game's save header key "CampaignSaveHeader" (a save under another name)
One exception (owner 2026-10-21: the hub and PvP maps ship inside the Multiplayer mod): multiplayer/hub_<build>.json and
multiplayer/base_<build>.json, and only when no unit in them has a player-given name (every unit name a game text key)."""
import json, os, re, sys, zipfile
MAP = re.compile(r'^(hub|base)_[0-9]{6,10}\.json$')


def shipped_map(parts, read):
    if len(parts) < 2 or parts[-2] != 'multiplayer' or not MAP.match(parts[-1]): return False
    try: s = json.loads(read())
    except ValueError: return False
    ec = (s.get('EntityComponentSaveData') or {}).get('EntityComponents') or {}
    for side in ((s.get('EntityContainerSaveData') or {}).get('UnitContainerSaveData') or {}).values():
        for g in (side.get('UnitSaveDataDictionary') or {}):
            for c in ec.get(g) or []:
                d = c.get('Data') if isinstance(c, dict) else None
                n = d.get('UnitNameTextKey') if isinstance(d, dict) else None
                if n is not None and not (isinstance(n, str) and n.startswith('TEXTKEY_')): return False
    return True
DIRS = {'saves', 'corruptsaves', 'cloud_backups', 'save_copies', 'mp_owners', 'profiles', 'instances', 'test_env'}
FILES = {'saveheadercache.json', 'outgame.json', 'usersetting.json', 'usercampaignlog.json', 'worlds.json', 'active.json',
         'pending.json', 'groups.json', 'cloud_state.tsv', 'mp_tags.txt', 'mp_worlds.txt', 'player.log', 'rig.log', 'dsload.log'}
hits = 0


def check(name, read):
    global hits
    parts = [p.lower() for p in name.replace('\\', '/').split('/') if p]
    why = None
    inner = [p.lower() for p in re.split(r'[/:]', name.replace('\\', '/')) if p]   # a zip member: "<zip>:<id>/<file>"
    if any(p in DIRS for p in parts[:-1]): why = 'player-data folder'
    elif parts and parts[-1] in FILES: why = 'player file'
    elif parts and parts[-1].endswith('.json') and b'"CampaignSaveHeader"' in read() and not shipped_map(inner, read):
        why = 'a game save (CampaignSaveHeader)'
    if why:
        hits += 1; print('HIT', name, '-', why)


for a in sys.argv[1:]:
    if a.endswith('.zip'):
        z = zipfile.ZipFile(a)
        for n in z.namelist():
            if not n.endswith('/'): check(f'{a}:{n}', lambda n=n: z.read(n))
    else:
        for r, ds, fs in os.walk(a):
            for f in fs:
                p = os.path.join(r, f); check(os.path.relpath(p, a), lambda p=p: open(p, 'rb').read())
print(f'{hits} file(s) of player data')
sys.exit(1 if hits else 0)
