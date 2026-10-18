#!/usr/bin/env python3
"""make_manifest.py <repo> <tag> <out file> <file>...: the release-manifest.json of one GitHub release (signed next by
sign_release.sh; the test release server relserver.py makes its own the same way). The loader (loader/src/dsl_update.c)
takes a file's sha256 only from a manifest that names the same repo, tag, id and version, signed by a trusted key.

Tag v<ver> = the loader kit (id "kit"): files Dungeon-Settlers-Modded-Mac.dmg / -Mac.zip (mac-arm64), -Windows.zip /
-Setup.exe (win-x64). Tag <id>-v<ver> = a mod: one file <id>-<ver>.zip (platform "any"). Any other file name: refused."""
import hashlib, json, os, re, sys

KIT = {'Dungeon-Settlers-Modded-Mac.dmg': 'mac-arm64', 'Dungeon-Settlers-Modded-Mac.zip': 'mac-arm64',
       'Dungeon-Settlers-Modded-Windows.zip': 'win-x64', 'Dungeon-Settlers-Modded-Setup.exe': 'win-x64'}


def manifest(repo, tag, files):
    m = re.fullmatch(r'(?:([a-z0-9][a-z0-9-]*)-)?v(\d+(?:\.\d+){1,5})', tag)
    if not m or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise ValueError(f'tag {tag!r} / repo {repo!r}: expected v<version> or <id>-v<version>, owner/repo')
    mid, ver = m.group(1) or 'kit', m.group(2)
    out = []
    for f in files:
        n = os.path.basename(f)
        plat = KIT.get(n) if mid == 'kit' else ('any' if n == f'{mid}-{ver}.zip' else None)
        if not plat:
            raise ValueError(f'{n}: not a file of release {tag} (kit: {", ".join(KIT)}; mod: {mid}-{ver}.zip)')
        b = open(f, 'rb').read()
        out.append({'name': n, 'platform': plat, 'sha256': hashlib.sha256(b).hexdigest(), 'size': len(b)})
    if not out or len({e['name'] for e in out}) != len(out):
        raise ValueError('no files, or the same file name twice')
    return json.dumps({'format': 1, 'repo': repo, 'tag': tag, 'id': mid, 'version': ver, 'files': out}, indent=1) + '\n'


if __name__ == '__main__':
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    try:
        text = manifest(sys.argv[1], sys.argv[2], sys.argv[4:])
    except ValueError as e:
        sys.exit(f'make_manifest: {e}')
    with open(sys.argv[3], 'w') as f:
        f.write(text)
