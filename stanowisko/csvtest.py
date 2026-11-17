#!/usr/bin/env python3
"""csvtest.py – test „Pobierz pomiary (CSV)” (0.5.5): dzień, miesiąc, rok jednym plikiem.
Mierzy czas i rozmiar, liczy wiersze danych i „brak danych”, sprawdza nagłówek i BOM.
Pobiera tak jak strona: /api/v1/logs/csv?from=…&to=… z sesją po zalogowaniu."""
import sys, time, datetime
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board

c = load(); sec = c["sekrety"]; b = Board(c["plytka"]["adres"], "", 5); ck = b.login(sec["user"], sec["pass"])
today = datetime.date.today()
cases = [("dzień", today, today),
         ("miesiąc", today.replace(day=1), today),
         ("rok", today.replace(month=1, day=1), today)]
for name, a, z in cases:
    t0 = time.time()
    _, data = b._req("GET", f"/api/v1/logs/csv?from={a}&to={z}", cookie=ck, timeout=180, raw=True)
    dt = time.time() - t0
    if isinstance(data, bytes):
        txt = data.decode("utf-8", "replace")
    else:
        txt = str(data)
    lines = [l for l in txt.lstrip("﻿").splitlines() if l.strip()]
    hdr = [l for l in lines if l.lower().startswith("data")]
    gaps = [l for l in lines if "brak danych" in l]
    rows = len(lines) - len(hdr) - len(gaps)
    print(f"{name:8s} {a}…{z}: {len(data) / 1024:.1f} kB w {dt:.1f} s, wierszy pomiarów {rows}, nagłówków {len(hdr)}, "
          f"„brak danych” {len(gaps)}, BOM {'tak' if txt.startswith(chr(0xfeff)) else 'NIE'}")
    if lines:
        print("   pierwszy:", lines[0][:90]); print("   ostatni: ", lines[-1][:90])
st = b._req("GET", "/api/v1/status", cookie=ck)
print("po teście: stan", st["tanks"][0]["state"], "uptime", st.get("uptime"))
b._req("POST", "/api/v1/logout", {}, cookie=ck)
