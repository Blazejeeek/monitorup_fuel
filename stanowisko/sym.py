#!/usr/bin/env python3
"""sym.py <mA|ack> [czekaj_s] – zadaj prad generatorem, odczekaj, pokaz stan sterownika z API."""
import sys, time, subprocess, json
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); s = c["sekrety"]
b = Board(c["plytka"]["adres"], s.get("token", ""), 10)
ST = {0: "NORMALNY (POPRAWNY)", 1: "MINIMALNY", 2: "KRYTYCZNY", 3: "ZA DUŻY (przepełnienie)", 4: "BŁĄD CZUJNIKA", 5: "WYŁĄCZONY", 6: "NAGŁY SPADEK"}
RED = {2: "czerwona świeci ciągle", 3: "czerwona miga 1×/s", 6: "czerwona miga 2×/s", 4: "czerwona miga 3×/s"}
arg = sys.argv[1]; wait = float(sys.argv[2]) if len(sys.argv) > 2 else 15
t0 = int(time.time())
if arg == "ack":
    bb = Board(c["plytka"]["adres"], "", 10); ck = bb.login(s["user"], s["pass"])
    bb._req("POST", "/api/v1/alarm/ack", {}, cookie=ck); bb._req("POST", "/api/v1/logout", {}, cookie=ck)
    print("Wysłano: wycisz buzer / skasuj alarm")
else:
    out = subprocess.run(["masim", "gen", arg], capture_output=True, text=True)
    print((out.stdout + out.stderr).strip().splitlines()[-1])
time.sleep(wait)
st = b.status(); t = st["tanks"][0]; a = st["alarm"]
ol = st.get("outputs") or st.get("out") or []
print(f"Sterownik: {t.get('mA'):.2f} mA, {t.get('cm'):.1f} cm ({t.get('pct'):.1f} %), stan {t.get('state')} = {ST.get(t.get('state'), '?')}")
lv = st.get("outLevel") or st.get("ol")
wy = st.get("outputs") if isinstance(st.get("outputs"), int) else None
print("Wyjścia:", json.dumps({k: st[k] for k in st if k.lower().startswith("out")}, ensure_ascii=False))
print(f"Alarm: aktywny={a.get('active')} skasowany={a.get('acked')} ostrzeżenie={a.get('warn')} · {RED.get(t.get('state'), 'czerwona zgaszona')}")
ev = [e for e in b.events() if e[0] >= t0 - 1]
for e in ev[:6]: print("  zdarzenie:", time.strftime('%H:%M:%S', time.localtime(e[0])), e[1], "–", e[2])
