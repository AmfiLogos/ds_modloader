#!/usr/bin/env python3
"""rig.py's guard process, with the rig's instance folder pointed at test_env/pair (weaponslot_pair.py sets
rig.__file__ to this file, so rig.start() spawns this guard instead of rig.py's own)."""
import os, sys
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig  # noqa: E402
rig.INST = os.path.join(os.environ.get('WEAPONSLOT_ROOT') or os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'test_env', 'pair')
assert sys.argv[1] == '_guard'
rig.guard(sys.argv[2])
