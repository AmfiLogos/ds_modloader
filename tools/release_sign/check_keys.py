#!/usr/bin/env python3
"""check_keys.py <kit release file>...: every loader inside the kit's release files trusts exactly the owner's release keys.

A loader built with -DDSL_SIGN_DEV_KEY (test builds, e.g. what mac_gate.sh builds) also trusts dev_keys/dev.pub, which is
public: if it shipped, anyone could sign updates for its players. For each file:
  .dmg  (hdiutil, hidden, read-only)  every .../Contents/Resources/dsml/dsload.dylib
  .zip  every member named dsload.dylib or baselib.dll (Mac app zip, Windows kit zip)
  .exe  the NSIS Setup.exe: its solid LZMA data (the whole kit) unpacked in memory
must hold both public keys of loader/src/dsl_sig.c (main + spare) and never the dev key; a file with no loader in it
fails too. Exit 0 = all good, 1 = stop the publish. sign_release.sh runs it for every kit tag (v<ver>)."""
import lzma, os, re, struct, subprocess, sys, tempfile, zipfile

TOP = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..'))
SRC = open(os.path.join(TOP, 'loader/src/dsl_sig.c')).read()
DEV = re.search(r'#define DSL_SIG_DEV_PUB "([^"]+)"', SRC).group(1).encode()
block = re.search(r'static const char \*const KEYS\[\] = \{(.*?)#ifdef DSL_SIGN_DEV_KEY', SRC, re.S).group(1)
REAL = [k.encode() for k in re.findall(r'"([A-Za-z0-9+/]{43}=)"', block)]
assert len(REAL) == 2 and DEV not in REAL, 'dsl_sig.c: expected 2 release keys before the dev key'


def judge(what, data):
    bad = []
    if DEV in data:
        bad.append('trusts the public DEV key')
    for i, k in enumerate(REAL):
        if k not in data:
            bad.append(f'misses release key {i} ({k.decode()})')
    print(('ok   ' if not bad else 'FAIL ') + what + ('' if not bad else ': ' + ', '.join(bad)))
    return not bad


def nsis_blob(d):
    m = d.find(b'\xef\xbe\xad\xdeNullsoftInst')
    if m < 0:
        return None
    off = m + 12 + 8 + 4                                   # first header, then the solid stream's 4-byte size
    try:
        return lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(d[off:off + 5] + b'\xff' * 8 + d[off + 5:], max_length=2 << 30)
    except lzma.LZMAError:
        return None


def check(f):
    n = os.path.basename(f); found = []
    if f.endswith('.dmg'):
        mnt = tempfile.mkdtemp(prefix='check_keys.')
        subprocess.run(['hdiutil', 'attach', '-nobrowse', '-readonly', '-noautoopen', '-mountpoint', mnt, f],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for dp, _, fs in os.walk(mnt):
                if 'dsload.dylib' in fs and dp.endswith('Contents/Resources/dsml'):
                    found.append(judge(f'{n}: {os.path.relpath(dp, mnt)}/dsload.dylib', open(os.path.join(dp, 'dsload.dylib'), 'rb').read()))
        finally:
            if subprocess.run(['hdiutil', 'detach', mnt, '-quiet']).returncode:
                subprocess.run(['hdiutil', 'detach', mnt, '-force', '-quiet'])
    elif f.endswith('.zip'):
        with zipfile.ZipFile(f) as z:
            for i in z.namelist():
                if os.path.basename(i) in ('dsload.dylib', 'baselib.dll'):
                    found.append(judge(f'{n}: {i}', z.read(i)))
    elif f.endswith('.exe'):
        b = nsis_blob(open(f, 'rb').read())
        if b is not None:
            found.append(judge(f'{n}: unpacked installer data', b))
    if not found:
        print(f'FAIL {n}: no loader found in it')
    return bool(found) and all(found)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ok = [check(f) for f in sys.argv[1:]]
    sys.exit(0 if all(ok) else 1)
