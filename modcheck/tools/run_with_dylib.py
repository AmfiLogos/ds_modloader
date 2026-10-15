#!/usr/bin/env python3
"""run_with_dylib.py <dylib> <script.py> [args...]: run a rig test script with rig.DYLIB pointed at <dylib>."""
import os, runpy, sys
RIG = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(0, RIG)
import rig
rig.DYLIB = os.path.abspath(sys.argv[1])
_lease = rig.lease                                       # leases go under the launcher prefix (rig/reserve.lock)
_held = {}                                               # pair -> our lane name: releases name it (night rule 10)
def _lease_c(lane, *a, **k):
    ln = lane if lane.startswith('launcher') else 'launcher_' + lane
    p = _lease(ln, *a, **k)
    if p: _held[p] = ln
    return p
rig.lease = _lease_c
_release = rig.release
rig.release = lambda pair, lane=None: _release(pair, lane or _held.get(pair) or 'launcher')
if os.path.basename(sys.argv[2]) == 'rig.py':       # rig.py itself: call the imported module (its DYLIB is the patched one)
    sys.argv = ['rig.py'] + sys.argv[3:]
    print('dylib', rig.DYLIB, flush=True); rig.main(); sys.exit(0)
script = os.path.abspath(sys.argv[2])
sys.argv = [script] + sys.argv[3:]
sys.path.insert(0, os.path.dirname(script))
print('dylib', rig.DYLIB, flush=True)
runpy.run_path(script, run_name='__main__')
