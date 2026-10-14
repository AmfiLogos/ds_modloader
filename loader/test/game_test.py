#!/usr/bin/env python3
"""Single hidden copy (L) with dsload + multiplayer + gcd-cdr + floor-logger + a foreign inserted dylib.
Runs the rig smoke (title, load save, order a move, fingerprints), then a soak of SOAK_S seconds (default 600):
process alive, sim tick advancing, no crash. Owner prefs only CHECKED (never restored from here)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lrig
X = 'L'
SOAK = int(os.environ.get('SOAK_S', '600'))
out = os.path.join(lrig.LROOT, 'test_env', 'smoke_out', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(out, exist_ok=True)
os.environ['LRIG_EXTRA_INSERT'] = os.path.join(lrig.LROOT, 'test_env', 'foreign.dylib')
os.environ['FOREIGN_OUT'] = os.path.join(lrig.idir(X), 'foreign.txt')
mj = os.path.join(lrig.idir(X), 'mp.json')
assert not os.path.exists(mj), 'mp.json present: smoke is single-player'
print('owner prefs identical before:', lrig.check_prefs(restore=False))
res = {}
try:
    lrig.start(X)
    res['smoke'] = lrig.smoke_one(X, out)
    t0 = time.time(); last = None; ticks = []
    lrig.send(X, {'cmd': 'set_speed', 'speed': 3})
    while time.time() - t0 < SOAK:
        time.sleep(30)
        if not lrig.alive(lrig.read_pid(X)):
            raise RuntimeError(f'process died after {time.time() - t0:.0f} s of soak')
        st = lrig.send(X, {'cmd': 'status'}, timeout=60)
        ticks.append(st.get('tick')); print(f'soak {time.time() - t0:5.0f} s: tick {st.get("tick")} scene {st.get("scene")} busy {st.get("busy")}', flush=True)
    res['soak'] = dict(seconds=round(time.time() - t0), ticks=ticks, advancing=all(b > a for a, b in zip(ticks, ticks[1:])))
    res['alive_at_end'] = lrig.alive(lrig.read_pid(X))
finally:
    lrig.stop(X)
print('owner prefs identical after:', lrig.check_prefs(restore=False))
json.dump(res, open(os.path.join(out, 'results.json'), 'w'), indent=1)
print('RESULT', json.dumps(res))
