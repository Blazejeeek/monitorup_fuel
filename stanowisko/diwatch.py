import sys, time
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); b = Board(c["plytka"]["adres"], c["sekrety"]["token"], 5)
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 120
prev = None; t0 = time.time(); n = 0
print(time.strftime("%H:%M:%S"), "podgląd wejść start", flush=True)
while time.time() - t0 < dur:
    try: d = b.status()["di"]
    except Exception: time.sleep(0.5); continue
    n += 1
    if d != prev:
        act = [f"IN{k+1}" for k in range(8) if d >> k & 1]
        print(time.strftime("%H:%M:%S"), f"di={d:3d}  aktywne: {', '.join(act) if act else 'żadne'}", flush=True); prev = d
    time.sleep(0.15)
print(time.strftime("%H:%M:%S"), f"koniec ({n} odczytów)")
