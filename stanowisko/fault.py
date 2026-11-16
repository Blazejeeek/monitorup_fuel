#!/usr/bin/env python3
"""fault.py [max_s] – czeka na BLAD CZUJNIKA (reczne odlaczenie A4), mierzy wyjscia, czeka na powrot."""
import sys, time
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); sec = c["sekrety"]; b = Board(c["plytka"]["adres"], "", 5)
ck = b.login(sec["user"], sec["pass"])
S = lambda: b._req("GET", "/api/v1/status", cookie=ck)
T = lambda: time.strftime("%H:%M:%S")
mx = float(sys.argv[1]) if len(sys.argv) > 1 else 600
print(f"[{T()}] czekam na odłączenie A4 (do {mx/60:.0f} min)", flush=True)
t0 = time.time()
while time.time() - t0 < mx:
    st = S(); t = st["tanks"][0]
    if t["state"] == 4: break
    time.sleep(0.5)
else:
    print(f"[{T()}] brak usterki – koniec czekania"); sys.exit(1)
tf = time.time()
print(f"[{T()}] BŁĄD CZUJNIKA wykryty: {t['mA']:.2f} mA", flush=True)
time.sleep(1.5)
smp = []; ts = time.time()
while time.time() - ts < 6:
    o = S()["out"]; smp.append((time.time() - ts, o)); time.sleep(0.02)
n = len(smp)
def bit(k): return [(t, (o >> k) & 1) for t, o in smp]
for k, name in [(0, "WY1 zielona"), (1, "WY2 pomarańczowa"), (2, "WY3 czerwona"), (3, "WY4 buzer"), (4, "WY5 przekaźnik usterki")]:
    v = bit(k); on = sum(x for _, x in v)
    rises = [t for (t, x), (_, p) in zip(v[1:], v) if x and not p]
    if on == 0: d = "wyłączone"
    elif on == n: d = "włączone ciągle"
    else:
        f = (len(rises) - 1) / (rises[-1] - rises[0]) if len(rises) > 1 else float("nan")
        d = f"miga {f:.2f}× na sekundę (włączone {on*100//n} % czasu)"
    print(f"  {name}: {d}")
st = S(); a = st["alarm"]; print(f"  próbek {n}; alarm aktywny={a.get('active')} ostrzeżenie={a.get('warn')}", flush=True)
print(f"[{T()}] czekam na ponowne podłączenie A4…", flush=True)
while time.time() - tf < mx:
    t = S()["tanks"][0]
    if t["state"] != 4: break
    time.sleep(1)
time.sleep(12)
st = S(); t = st["tanks"][0]
print(f"[{T()}] po podłączeniu: {t['mA']:.2f} mA, {t['cm']:.1f} cm, stan {t['state']}, out {st['out']}")
for e in [e for e in b._req('GET', '/api/v1/events', cookie=ck).get('events', []) if e[0] >= tf - 5][:6]:
    print("  zdarzenie:", time.strftime('%H:%M:%S', time.localtime(e[0])), e[1], "–", e[2])
b._req("POST", "/api/v1/logout", {}, cookie=ck)
