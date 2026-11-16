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
    st = b._req("GET", "/api/v1/status", cookie=ck); smp.append((time.time() - t0, st["out"])); time.sleep(0.02)
b._req("POST", "/api/v1/logout", {}, cookie=ck)
n = len(smp)
dt = (smp[-1][0] - smp[0][0]) / max(1, n - 1)
for k, name in [(2, "WY3 czerwona"), (3, "WY4 buzer")]:
    v = [(t, (o >> k) & 1) for t, o in smp]; on = sum(x for _, x in v)
    rises = [t for (t, x), (_, p) in zip(v[1:], v) if x and not p]
    if on == 0: print(f"{name}: wyłączone ({n} próbek co {dt*1000:.0f} ms)")
    elif on == n: print(f"{name}: włączone ciągle ({n} próbek co {dt*1000:.0f} ms)")
    else:
        f = (len(rises) - 1) / (rises[-1] - rises[0]) if len(rises) > 1 else float("nan")
        print(f"{name}: miga {f:.2f}× na sekundę (włączone {on*100//n} % czasu, {n} próbek co {dt*1000:.0f} ms)")
