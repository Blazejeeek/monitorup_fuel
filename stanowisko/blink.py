#!/usr/bin/env python3
"""blink.py [s] – mierzy rytm czerwonej kontrolki (WY3) z pola 'out' w /api/v1/status (stan faktyczny wyjsc)."""
import sys, time
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); sec = c["sekrety"]; b = Board(c["plytka"]["adres"], "", 5); ck = b.login(sec["user"], sec["pass"])
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 6
smp = []; t0 = time.time()
while time.time() - t0 < dur:
    st = b._req("GET", "/api/v1/status", cookie=ck); smp.append((time.time() - t0, (st["out"] >> 2) & 1)); time.sleep(0.02)
b._req("POST", "/api/v1/logout", {}, cookie=ck)
n = len(smp); on = sum(v for _, v in smp)
rises = [t for (t, v), (_, p) in zip(smp[1:], smp) if v and not p]
dt = sum(b2 - a2 for (a2, _), (b2, _) in zip(smp, smp[1:])) / max(1, n - 1)
if on == 0: print(f"WY3: zgaszona ({n} próbek, co {dt*1000:.0f} ms)")
elif on == n: print(f"WY3: świeci ciągle ({n} próbek, co {dt*1000:.0f} ms)")
else:
    f = (len(rises) - 1) / (rises[-1] - rises[0]) if len(rises) > 1 else float('nan')
    print(f"WY3: miga ok. {f:.2f}× na sekundę ({len(rises)} zapaleń w {dur:.0f} s, {n} próbek co {dt*1000:.0f} ms, świeci {on*100//n} % czasu)")
