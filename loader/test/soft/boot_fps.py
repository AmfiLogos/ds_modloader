import os, sys, time, json
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r
X='S'; os.environ.setdefault('DSL_PROFILE', os.path.join(r.idir(X), 'profile1.json'))
fps=int(sys.argv[1]); window = sys.argv[2] == 'window'
t0=time.time(); r.start(X, window=window, fps=fps); marks={}
while time.time()-t0<120:
    try: s=r.send(X,{'cmd':'status'},timeout=10,connect_wait=0)
    except Exception: time.sleep(0.1); continue
    for k,c in (('first',True),('boot_ready',s.get('boot_ready')),('title',s.get('scene')==1 and not s.get('busy'))):
        if c and k not in marks: marks[k]=(round(time.time()-t0,2), s.get('frames'))
    if 'title' in marks: break
    time.sleep(0.05)
print(fps, 'window' if window else 'batch', marks); r.stop(X)
