#!/usr/bin/env python3
"""ls_forget.py <dir>...: unregisters from LaunchServices every app bundle a test or build made under <dir>.

Test and build copies of "Dungeon Settlers Modded" (same name, same bundle id) stay registered after a run (a start
through LaunchServices, lsregister -f, Spotlight indexing them): Spotlight and Launchpad then list many entries with the
same name, and LaunchServices may pick one of them for the bundle id (owner 2026-10-24, 77 found). Unregistered: every
*.app under <dir> on disk, and every registered path under <dir> (a bundle moved, renamed or on a detached disk image
stays registered under its old path). Last line: "ls_forget: <n> unregistered"."""
import os, re, subprocess, sys

LSR = '/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister'


def forget(dirs):
    roots = [os.path.realpath(d).rstrip('/') + '/' for d in dirs if d]
    paths = set()
    for r in roots:
        for d, sub, _ in os.walk(r):
            for s in list(sub):
                if s.endswith('.app'):
                    paths.add(os.path.join(d, s)); sub.remove(s)
    dump = subprocess.run([LSR, '-dump'], capture_output=True, text=True, errors='replace').stdout
    for m in re.finditer(r'^path:\s+(.*?)(?: \(0x[0-9a-f]+\))?$', dump, re.M):
        p = m.group(1)
        if any(p.startswith(r) or os.path.realpath(p).startswith(r) for r in roots):
            paths.add(p)
    n = 0
    for p in sorted(paths):
        if subprocess.run([LSR, '-u', p], capture_output=True).returncode == 0:
            n += 1
    return n


if __name__ == '__main__':
    print(f'ls_forget: {forget(sys.argv[1:])} unregistered')
