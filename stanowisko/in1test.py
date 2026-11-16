"""Test wyciszenia przyciskiem IN1: stan KRYTYCZNY (5.2 mA), czekanie na nacisniecie IN1 (do 3 min), powrot 14 mA."""
import sys, time, subprocess
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); b = Board(c["plytka"]["adres"], c["sekrety"]["token"], 5)
T = lambda: time.strftime("%H:%M:%S")
subprocess.run(["python3", "dropen.py", "0"])
subprocess.run(["masim", "gen", "5.2"], capture_output=True)
print(f"[{T()}] zadajnik 5.2 mA – czekam na stan KRYTYCZNY", flush=True)
t0 = time.time()
while time.time() - t0 < 60:
    st = b.status()
    if st["tanks"][0]["state"] == 2 and st["out"] & 8: break
    time.sleep(0.5)
print(f"[{T()}] KRYTYCZNY, buzer gra (out {st['out']}) – NACIŚNIJ IN1", flush=True)
t1 = time.time(); ok = False
while time.time() - t1 < 180:
    st = b.status()
    if st.get("buzzMuted") and not st["out"] & 8:
        ok = True; break
    time.sleep(0.3)
st = b.status()
print(f"[{T()}] {'WYCISZONE' if ok else 'brak wyciszenia w 3 min'}: out {st['out']}, buzzMuted {st.get('buzzMuted')}, alarm {st['alarm']}", flush=True)
for e in [e for e in b.events() if e[0] >= t1 - 2][:4]: print("  zdarzenie:", time.strftime('%H:%M:%S', time.localtime(e[0])), e[1], "–", e[2])
subprocess.run(["masim", "gen", "14"], capture_output=True)
time.sleep(25); st = b.status(); t = st["tanks"][0]
print(f"[{T()}] powrót: {t['mA']} mA, {t['cm']} cm, stan {t['state']}, out {st['out']}", flush=True)
time.sleep(660)
subprocess.run(["python3", "dropen.py", "1"])
st = b.status(); print(f"[{T()}] koniec: stan {st['tanks'][0]['state']}, out {st['out']}")
