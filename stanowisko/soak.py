#!/usr/bin/env python3
"""soak.py [godziny] – próba długotrwała sterownika (uruchamiać na Pi przez nohup).
Co 60 s: status z API do ~/soak/soak_RRRRMMDD_HHMM.csv; co 10 min odczyt Modbus; po północy kontrola podsumowań.
Zdarzenia (restart, błąd odczytu, zmiana stanu, niezgodność Modbus) także do .log. Analiza: soak.py --raport plik.csv"""
import sys, os, time, json, csv, subprocess
sys.path.insert(0, "/home/glimmer/rpi"); sys.path.insert(0, os.path.expanduser("~"))
from masim.config import load
from masim.board import Board

def raport(fn):
    rows = list(csv.DictReader(open(fn)))
    ok = [r for r in rows if r["blad"] == ""]
    print(f"Plik {fn}: {len(rows)} próbek, od {rows[0]['czas']} do {rows[-1]['czas']}, błędów odczytu {len(rows) - len(ok)}")
    up = [int(r["up"]) for r in ok]; rst = sum(1 for a, b in zip(up, up[1:]) if b < a)
    print(f"Restarty sterownika: {rst}")
    heap = [int(r["heap"]) for r in ok if r["heap"]]
    if heap: print(f"Wolna pamięć (heap): start {heap[0]}, min {min(heap)}, koniec {heap[-1]}  (zmiana {heap[-1] - heap[0]:+d} B)")
    ma = [float(r["mA"]) for r in ok]; print(f"Prąd: min {min(ma):.3f}, max {max(ma):.3f} mA (rozrzut {max(ma) - min(ma):.3f})")
    t = [float(r["rtcTemp"]) for r in ok if r["rtcTemp"]]; print(f"Temperatura RTC: min {min(t):.1f}, max {max(t):.1f} °C")
    st = {}; [st.__setitem__(r["stan"], st.get(r["stan"], 0) + 1) for r in ok]; print(f"Stany (liczba minut): {st}")
    print(f"Ethernet bez łącza: {sum(1 for r in ok if r['eth_link'] != 'True')} min; karta SD nie OK: {sum(1 for r in ok if r['sd_ok'] != 'True')} min")
    mb = [r for r in ok if r["modbus"]]; print(f"Modbus: {len(mb)} odczytów, niezgodnych/błędnych {sum(1 for r in mb if r['modbus'] != 'OK')}")

if len(sys.argv) > 2 and sys.argv[1] == "--raport": raport(sys.argv[2]); sys.exit()

hours = float(sys.argv[1]) if len(sys.argv) > 1 else 24
c = load(); sec = c["sekrety"]; b = Board(c["plytka"]["adres"], sec["token"], 8)
os.makedirs(os.path.expanduser("~/soak"), exist_ok=True)
base = os.path.expanduser(time.strftime("~/soak/soak_%Y%m%d_%H%M"))
fcsv, flog = open(base + ".csv", "w", newline=""), open(base + ".log", "a")
W = csv.writer(fcsv)
W.writerow(["czas", "up", "fw", "reset", "heap", "psram", "mA", "cm", "stan", "alarm", "out", "di", "sd_ok", "sd_used", "eth_link", "eth_ip", "rtcTemp", "modbus", "blad"])
def log(msg):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + msg; print(line, flush=True); flog.write(line + "\n"); flog.flush()
log(f"Start próby {hours} h, pliki {base}.csv/.log")
end = time.time() + hours * 3600; last_up = None; last_state = None; n = 0; last_day = time.strftime("%d"); summary_check = None
while time.time() < end:
    t0 = time.time(); row = [time.strftime("%Y-%m-%d %H:%M:%S")]
    try:
        s = b.status(); t = s["tanks"][0]; e = s["net"]["eth"]
        mb = ""
        if n % 10 == 0:
            try:
                from modbus_test import Modbus, i16
                m = Modbus("192.168.4.1", timeout=3); z = m.input_regs(100, 4); fw = m.input_regs(0, 1)[0]; m.close()
                mb = "OK" if abs(i16(z[0]) / 10 - t["cm"]) <= 0.5 and z[3] == t["state"] else f"NIEZGODNE mb={i16(z[0]) / 10}/{z[3]} api={t['cm']}/{t['state']}"
            except Exception as x: mb = f"BLAD {type(x).__name__}"
            if mb != "OK": log("Modbus: " + mb)
        row += [s.get("up"), s.get("fw"), s.get("reset"), s.get("heap"), s.get("psram"), t["mA"], t["cm"], t["state"], s["alarm"]["active"], s["out"], s["di"],
                s["sd"]["ok"], s["sd"].get("usedMB"), e.get("link"), e.get("ip"), s.get("rtcTemp"), mb, ""]
        if last_up is not None and s["up"] < last_up: log(f"RESTART sterownika: czas pracy {last_up} → {s['up']} s, powód {s.get('reset')}")
        if last_state is not None and t["state"] != last_state: log(f"Zmiana stanu {last_state} → {t['state']} ({t['cm']} cm, {t['mA']} mA)")
        last_up, last_state = s["up"], t["state"]
        if time.strftime("%d") != last_day: last_day = time.strftime("%d"); summary_check = time.time() + 900   # 15 min po polnocy
        if summary_check and time.time() > summary_check:
            summary_check = None
            try:
                ck = Board(c["plytka"]["adres"], "", 10); cookie = ck.login(sec["user"], sec["pass"])
                sm = ck._req("GET", "/api/v1/summary?view=month&y=" + time.strftime("%Y") + "&m=" + str(int(time.strftime("%m"))), cookie=cookie)
                fl = ck._req("GET", "/api/v1/logs/files", cookie=cookie); ck._req("POST", "/api/v1/logout", {}, cookie=cookie)
                log("Po północy – podsumowanie miesiąca: " + json.dumps(sm, ensure_ascii=False)[:600])
                log("Po północy – pliki logów: " + json.dumps(fl, ensure_ascii=False)[:400])
            except Exception as x: log(f"Po północy – błąd odczytu podsumowań: {x}")
    except Exception as x:
        row += [""] * 17 + [type(x).__name__ + ": " + str(x)[:80]]; log("Błąd odczytu API: " + str(x)[:120])
    W.writerow(row); fcsv.flush(); n += 1
    time.sleep(max(1, 60 - (time.time() - t0)))
log("Koniec próby"); fcsv.close(); raport(base + ".csv")
