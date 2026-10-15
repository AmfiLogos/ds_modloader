#!/usr/bin/env python3
"""Task 4 pair test: the multiplayer team's own soak (~/ds_multiplayer/rig/soak_test.py, read at run time, never
edited) on two of OUR copies (M hosts, N joins) that run dsload + the merged modules instead of dsrig.dylib.
The pair lease comes from the real rig (night rules: lane 'loader', refused while rig/release.lock exists); its
port is used. Output goes to test_env/build/core_test/ (our folder), not to ~/ds_multiplayer.
  pair_soak.py --minutes 10 [soak_test.py options]"""
import importlib.util, os, sys, types
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lrig
spec = importlib.util.spec_from_file_location('realrig', os.path.expanduser('~/ds_multiplayer/rig/rig.py'))
realrig = importlib.util.module_from_spec(spec); spec.loader.exec_module(realrig)

os.environ['DSRIG_MAX_PAIRS'] = '2'                   # night rules: at most 2 leased pairs machine-wide
MAP = {}
shim = types.ModuleType('rig')
shim.__dict__.update({k: v for k, v in lrig.__dict__.items() if not k.startswith('__')})
shim.ROOT = os.path.join(lrig.LROOT, 'test_env')            # soak writes <ROOT>/build/core_test/...
shim.DYLIB = 'dsload.dylib (per copy) + modules'
shim.PAIR_PORTS = realrig.PAIR_PORTS
shim._with_leases = realrig._with_leases
def lease(lane, pid=None):
    p = realrig.lease('loader', pid)
    if p:
        MAP.update({p[0]: 'M', p[1]: 'N'})
        old = lrig.pair_port
        lrig.pair_port = lambda x: realrig.PAIR_PORTS[p] if x in ('M', 'N') else old(x)   # fix_mp_port keeps M/N on the leased port
        def keep(x, p=p):                                  # lrig.start / send mark OUR pair's lease as used
            realrig._with_leases(lambda d: d.get(p, {}).update(used=__import__('time').time()))
        lrig.touch = keep
        print(f'[pair_soak] leased {p}: {p[0]} -> M, {p[1]} -> N, port {realrig.PAIR_PORTS[p]}', flush=True)
    return p
shim.lease = lease
shim.release = realrig.release
for n in ('send', 'start', 'stop', 'wait_for', 'idir', 'read_pid', 'sockpath', 'pidfile', 'touch'):
    f = getattr(lrig, n, None)
    if f:
        setattr(shim, n, (lambda f: lambda x, *a, **k: f(MAP.get(x, x), *a, **k))(f))
shim.touch = lambda x: lrig.touch(x)
sys.modules['rig'] = shim
src = os.path.expanduser('~/ds_multiplayer/rig/soak_test.py')
sys.argv = [src] + sys.argv[1:]
code = compile(open(src).read(), src, 'exec')
g = {'__name__': '__main__', '__file__': src}
exec(code, g)
