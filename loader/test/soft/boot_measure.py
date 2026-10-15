#!/usr/bin/env python3
"""Measure a hidden-window boot of test copy S: process start -> title ready (scene 1, not busy)."""
import os, sys, time, json
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r
X = sys.argv[1] if len(sys.argv) > 1 else 'S'
os.environ.setdefault('DSL_PROFILE', os.path.join(r.idir(X), 'profile1.json'))
t0 = time.time()
r.start(X, window=True, fps=60)
seen = {}
while time.time() - t0 < 180:
    try:
        s = r.send(X, {'cmd': 'status'}, timeout=10, connect_wait=0)
    except Exception as e:
        time.sleep(0.2); continue
    key = (s.get('boot_ready'), s.get('scene'), s.get('busy'))
    if key not in seen:
        seen[key] = round(time.time() - t0, 2); print(seen[key], json.dumps(s)[:200], flush=True)
    if s.get('scene') == 1 and not s.get('busy'):
        break
    time.sleep(0.05)
print('title ready after', round(time.time() - t0, 2), 's')
