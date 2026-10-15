import os, sys, time, subprocess, signal, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import soft_test as st
out = sys.argv[1]
g = threading.Thread(target=st.guard, daemon=True); g.start()
t0 = time.time(); pid = st.start_a('profile1.json'); wid = None; n = 0
while time.time() - t0 < 16:
    if wid is None:
        ws = [w for w in st.r.windows([pid]) if w['w'] > 300]
        if ws: wid = ws[0]['id']; print('window', ws[0], 'at', round(time.time() - t0, 2), flush=True)
    if wid:
        f = os.path.join(out, f'e_{time.time() - t0:06.2f}.png')
        rc = subprocess.run(['screencapture', '-x', '-o', f'-l{wid}', f], capture_output=True, text=True)
        n += 1
        if n < 3 or n % 10 == 0: print(round(time.time()-t0,2), rc.returncode, rc.stderr.strip(), os.path.exists(f), flush=True)
    time.sleep(0.25)
st.stop_guard.set(); os.kill(pid, signal.SIGKILL); print('captures', n, 'violations', st.violations)
