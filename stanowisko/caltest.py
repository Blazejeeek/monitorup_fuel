"""Kalibracja +10 cm, po 3 min z powrotem (-10 cm), obserwacja zdarzen 4 min; na koniec przesuniecie jak przed testem."""
import sys, time, json
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); s = c["sekrety"]; b = Board(c["plytka"]["adres"], "", 10)
def sess(): return b.login(s["user"], s["pass"])
T = lambda: time.strftime("%H:%M:%S")
ck = sess(); off0 = b._req("GET", "/api/v1/config", cookie=ck)["tanks"][0]["offset"]
st = b._req("GET", "/api/v1/status", cookie=ck)["tanks"][0]; lv = st["cm"]; t0 = time.time()
print(f"[{T()}] start: {lv} cm, przesunięcie {off0}")
r = b._req("POST", "/api/v1/calibrate", {"tank": 0, "point": "level", "cm": round(lv + 10, 1)}, cookie=ck); print(f"[{T()}] kalibracja +10 cm:", r)
b._req("POST", "/api/v1/logout", {}, cookie=ck); time.sleep(180)
ck = sess(); st = b._req("GET", "/api/v1/status", cookie=ck)["tanks"][0]
r = b._req("POST", "/api/v1/calibrate", {"tank": 0, "point": "level", "cm": round(st["cm"] - 10, 1)}, cookie=ck); print(f"[{T()}] kalibracja -10 cm:", r)
b._req("POST", "/api/v1/logout", {}, cookie=ck); time.sleep(240)
ck = sess()
b._req("POST", "/api/v1/config", {"tanks": [{"offset": off0}, {}]}, cookie=ck)
st = b._req("GET", "/api/v1/status", cookie=ck); t = st["tanks"][0]
print(f"[{T()}] koniec: {t['cm']} cm, stan {t['state']}, spadek {t['drop']}, refilling {t['stats'].get('refilling')}, out {st['out']}")
for e in reversed([e for e in b._req("GET", "/api/v1/events", cookie=ck)["events"] if e[0] >= t0 - 400]):
    print("  ", time.strftime('%H:%M:%S', time.localtime(e[0])), e[1], "–", e[2])
f = b._req("GET", "/api/v1/logs/files", cookie=ck); print("pliki logów:", json.dumps(f, ensure_ascii=False)[:300])
b._req("POST", "/api/v1/logout", {}, cookie=ck)
