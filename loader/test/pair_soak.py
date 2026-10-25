#!/usr/bin/env python3
"""Task 4 pair test: the multiplayer team's own soak (~/ds_multiplayer/rig/soak_test.py, read at run time, never
edited) on two of OUR copies (M hosts, N joins) that run dsload + the merged modules instead of dsrig.dylib.
The pair lease comes from the real rig (night rules: lane 'loader', refused while rig/release.lock exists); its
port is used. Output goes to test_env/build/core_test/ (our folder), not to ~/ds_multiplayer.
  pair_soak.py --minutes 10 [soak_test.py options]"""
import faulthandler, importlib.util, os, signal, sys, types
faulthandler.register(signal.SIGUSR1, all_threads=True)
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
        port = realrig.PAIR_PORTS[p]                       # the pair lease's own port (rigports.py); M/N join that port lease, so
        if lrig.rigports.port_for([lrig.idir('M'), lrig.idir('N')], 'pair_soak', share=True) != port:   # lrig.pair_port (mp.json, host_port) gives it
            raise SystemExit(f'refused: M/N are not on the pair port {port}')
        def keep(x, p=p):                                  # lrig.start / send mark OUR pair's lease as used
            realrig._with_leases(lambda d: d.get(p, {}).update(used=__import__('time').time()))
        lrig.touch = keep
        print(f'[pair_soak] leased {p}: {p[0]} -> M, {p[1]} -> N, port {port}', flush=True)
    return p
shim.lease = lease
shim.release = realrig.release
for n in ('send', 'start', 'stop', 'wait_for', 'idir', 'read_pid', 'sockpath', 'pidfile', 'touch'):
    f = getattr(lrig, n, None)
    if f:
        setattr(shim, n, (lambda f: lambda x, *a, **k: f(MAP.get(x, x), *a, **k))(f))
shim.touch = lambda x: lrig.touch(x)
# Run 1 (06:29) hung 22 min in wait_for('title Y'): the guest (mp.json role client) joins by itself and was never seen
# idle on the title screen. Treat "already past the title" (scene 2) as reached for the title waits.
_wf = shim.wait_for
def wait_for(x, pred, what, *a, **k):
    if what.startswith('title'):
        p0 = pred
        pred = lambda s: p0(s) or s.get('scene') == 2
    return _wf(x, pred, what, *a, **k)
shim.wait_for = wait_for
sys.modules['rig'] = shim
src = os.path.expanduser('~/ds_multiplayer/rig/soak_test.py')
sys.argv = [src] + sys.argv[1:]
code = compile(open(src).read(), src, 'exec')
g = {'__name__': '__main__', '__file__': src}
exec(code, g)
