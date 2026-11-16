#!/usr/bin/env python3
"""MonitorUp.pl – test Modbus TCP (sama biblioteka standardowa Pythona).

Uruchamianie na Pi masim (porównanie z API sterownika przez masim):
  python3 modbus_test.py                 # odczyty FC01/02/03/04, wyjątki, Unit ID, kilku klientów, zapis zablokowany
  python3 modbus_test.py --zapis         # + zapis FC05/06/16: włącza zapis przez API na czas testu, przywraca wartości
  python3 modbus_test.py --dynamika      # + zadajnik: MINIMALNY i KRYTYCZNY widoczne w Modbus, kasowanie alarmu cewką 100
  python3 modbus_test.py --host 192.168.0.12 --bez-api   # sam Modbus z dowolnego komputera w sieci

Wynik: lista testów OK / BŁĄD i podsumowanie; kod wyjścia 0 = wszystko OK.
"""
import argparse, socket, struct, subprocess, sys, time, math

# ---------------- klient Modbus TCP ----------------
class ModbusError(Exception):
    def __init__(self, fc, code):
        super().__init__(f"wyjątek Modbus {code:02d} (funkcja {fc:02X})"); self.code = code

class Modbus:
    def __init__(self, host, port=502, unit=1, timeout=3.0):
        self.host, self.port, self.unit, self.tid, self.timeout = host, port, unit, 0, timeout
        self.reconnects = 0
        self.s = socket.create_connection((host, port), timeout=timeout)
    def reconnect(self):
        try: self.s.close()
        except Exception: pass
        time.sleep(1)
        self.s = socket.create_connection((self.host, self.port), timeout=self.timeout); self.reconnects += 1
    def close(self): self.s.close()
    def _recv(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c: raise ConnectionError("połączenie zamknięte przez sterownik")
            b += c
        return b
    def req(self, pdu, unit=None):
        try: return self._req(pdu, unit)
        except (ConnectionError, BrokenPipeError, ConnectionResetError):
            self.reconnect(); return self._req(pdu, unit)   # zmiana ustawien Modbus restartuje serwer w sterowniku
    def _req(self, pdu, unit=None):
        self.tid = (self.tid + 1) & 0xFFFF
        u = self.unit if unit is None else unit
        self.s.sendall(struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, u) + pdu)
        tid, pid, ln, ur = struct.unpack(">HHHB", self._recv(7))
        body = self._recv(ln - 1)
        if tid != self.tid: raise ValueError(f"zły identyfikator transakcji {tid} != {self.tid}")
        if body[0] & 0x80: raise ModbusError(body[0] & 0x7F, body[1])
        return body
    def _regs(self, fc, a, n, unit=None):
        b = self.req(struct.pack(">BHH", fc, a, n), unit)
        return list(struct.unpack(f">{n}H", b[2:2 + 2 * n]))
    def _bits(self, fc, a, n):
        b = self.req(struct.pack(">BHH", fc, a, n))
        return [(b[2 + i // 8] >> (i % 8)) & 1 for i in range(n)]
    def input_regs(self, a, n, unit=None): return self._regs(4, a, n, unit)
    def holding(self, a, n): return self._regs(3, a, n)
    def coils(self, a, n): return self._bits(1, a, n)
    def discrete(self, a, n): return self._bits(2, a, n)
    def write_reg(self, a, v): self.req(struct.pack(">BHH", 6, a, v & 0xFFFF))
    def write_regs(self, a, vals): self.req(struct.pack(f">BHHB{len(vals)}H", 16, a, len(vals), 2 * len(vals), *[v & 0xFFFF for v in vals]))
    def write_coil(self, a, v): self.req(struct.pack(">BHH", 5, a, 0xFF00 if v else 0))

def i16(v): return v - 0x10000 if v & 0x8000 else v
def f32(hi, lo): return struct.unpack(">f", struct.pack(">HH", hi, lo))[0]

# ---------------- wyniki ----------------
WYN = []
def ok(nazwa, warunek, szczegoly=""):
    WYN.append((nazwa, bool(warunek)))
    print(f"  {'OK   ' if warunek else 'BŁĄD '} {nazwa}" + (f"  – {szczegoly}" if szczegoly else ""), flush=True)
    return warunek
def blisko(a, b, tol): return a is not None and b is not None and abs(a - b) <= tol

# ---------------- API sterownika (przez masim na Pi) ----------------
class Api:
    def __init__(self):
        sys.path.insert(0, "/home/glimmer/rpi")
        from masim.config import load
        from masim.board import Board
        c = load(); self.sec = c["sekrety"]; self.b = Board(c["plytka"]["adres"], "", 10); self.ck = None
    def _ck(self):
        if not self.ck: self.ck = self.b.login(self.sec["user"], self.sec["pass"])
        return self.ck
    def status(self): return self.b._req("GET", "/api/v1/status", cookie=self._ck())
    def config(self): return self.b._req("GET", "/api/v1/config", cookie=self._ck())
    def set_config(self, d): return self.b._req("POST", "/api/v1/config", d, cookie=self._ck())
    def ack(self): return self.b._req("POST", "/api/v1/alarm/ack", {}, cookie=self._ck())
    def close(self):
        if self.ck:
            try: self.b._req("POST", "/api/v1/logout", {}, cookie=self.ck)
            except Exception: pass

def gen(ma): subprocess.run(["masim", "gen", str(ma)], capture_output=True)

# ---------------- testy ----------------
def test_odczyty(m, api):
    print("\n== Rejestry wejściowe (FC04) – urządzenie")
    r = m.input_regs(0, 15)
    st = api.status() if api else None
    ok("FC04 0: wersja programu", r[0] >= 50, f"{r[0]} = {r[0] // 100}.{r[0] // 10 % 10}.{r[0] % 10}")
    if st:
        fw = st.get("fw", ""); exp = int(fw.replace(".", "")) if fw.replace(".", "").isdigit() else None
        ok("wersja zgodna z API", exp == r[0], f"API fw {fw}")
    up = (r[2] << 16) | r[3]
    ok("FC04 2–3: czas pracy", up > 0, f"{up} s")
    if st: ok("czas pracy zgodny z API (±5 s)", blisko(up, st.get("up"), 5), f"API {st.get('up')} s")
    t_mb = time.mktime((r[4], r[5], r[6], r[7], r[8], r[9], 0, 0, -1))
    ok("FC04 4–9: data i godzina sterownika", 2025 <= r[4] <= 2100, f"{r[4]}-{r[5]:02d}-{r[6]:02d} {r[7]:02d}:{r[8]:02d}:{r[9]:02d}")
    ok("zegar sterownika zgodny z zegarem Pi (±60 s)", abs(t_mb - time.time()) <= 60, f"różnica {t_mb - time.time():+.0f} s")
    bits = r[1]
    ok("FC04 1: status – karta SD", bool(bits & 1) == bool(st["sd"]["ok"]) if st else bool(bits & 1), f"0b{bits:010b}")
    if st: ok("FC04 1: status – alarm aktywny zgodny z API", bool(bits & 128) == bool(st["alarm"]["active"]))
    if st: ok("FC04 11: wyjścia = API out", r[11] == st["out"] or abs(r[11] - st["out"]) in (4, 8, 12), f"Modbus {r[11]}, API {st['out']} (WY3/WY4 mogą migać)")
    if st: ok("FC04 12: wejścia = API di", r[12] == st["di"], f"{r[12]}")
    ok("FC04 14: temperatura RTC", r[14] != 0x8000 and -200 < i16(r[14]) < 800, f"{i16(r[14]) / 10:.1f} °C")

    print("\n== Rejestry wejściowe (FC04) – zbiornik 1 (100–129)")
    z = m.input_regs(100, 30)
    st = api.status() if api else None
    t = st["tanks"][0] if st else None
    cm, pct, ma = i16(z[0]) / 10, i16(z[1]) / 10, i16(z[2]) / 100
    ok("100 poziom cm ×10", z[0] != 0x8000, f"{cm} cm")
    ok("101 wypełnienie % ×10", 0 <= pct <= 100, f"{pct} %")
    ok("102 prąd mA ×100", 3 < ma < 22, f"{ma} mA")
    if t:
        ok("poziom zgodny z API (±0,5 cm)", blisko(cm, t["cm"], 0.5), f"API {t['cm']}")
        ok("prąd zgodny z API (±0,03 mA)", blisko(ma, t["mA"], 0.03), f"API {t['mA']}")
        ok("103 stan zgodny z API", z[3] == t["state"], f"{z[3]}")
    ok("104 flagi: pomiar ważny", bool(z[4] & 2), f"0b{z[4]:03b}")
    fcm, fpct, fma = f32(z[8], z[9]), f32(z[10], z[11]), f32(z[12], z[13])
    ok("108–109 poziom FLOAT32 = INT16 (±0,1)", blisko(fcm, cm, 0.1), f"{fcm:.2f} cm")
    ok("110–111 % FLOAT32", blisko(fpct, pct, 0.1), f"{fpct:.2f} %")
    ok("112–113 mA FLOAT32", blisko(fma, ma, 0.01), f"{fma:.3f} mA")
    ok("118 prognoza = brak (65535, usunięta w 0.4.4)", z[18] == 0xFFFF)
    vol = (z[20] << 16) | z[21]
    ok("120–121 objętość: bez litrów = 0xFFFFFFFF", vol == 0xFFFFFFFF if t and not t.get("volEn") else True, f"{vol:#x}")
    z2 = m.input_regs(203, 1)[0]
    ok("203 zbiornik 2: stan WYŁĄCZONY (5)", z2 == 5, f"{z2}")

    print("\n== Wejścia dyskretne (FC02) i cewki (FC01)")
    d = m.discrete(0, 24)
    st = api.status() if api else None
    if st:
        t = st["tanks"][0]
        ok("FC02 0–7 = wejścia DI", sum(b << i for i, b in enumerate(d[:8])) == st["di"])
        ok("FC02 8 alarm aktywny = API", d[8] == int(bool(st["alarm"]["active"])))
        exp = {2: 10, 1: 11, 3: 12, 4: 13}.get(t["state"])
        ok("FC02 10–13 bit stanu zbiornika 1", all(d[10 + k] == int(exp == 10 + k) for k in range(4)), f"{d[10:14]}, stan {t['state']}")
        ok("FC02 18 karta SD = API", d[18] == int(bool(st["sd"]["ok"])))
        ok("FC02 20 nagły spadek = API", d[20] == int(bool(t.get("drop"))))
    c = m.coils(0, 8)
    if st: ok("FC01 0–7 = wyjścia (mogą migać)", abs(sum(b << i for i, b in enumerate(c)) - st["out"]) in (0, 4, 8, 12), f"{c}")

    print("\n== Rejestry holding (FC03)")
    h = m.holding(100, 10)
    cf = api.config()["tanks"][0] if api else None
    if cf:
        ok("100 próg KRYTYCZNY = config", blisko(i16(h[0]) / 10, cf["thrCrit"], 0.05), f"{i16(h[0]) / 10} cm")
        ok("101 próg MINIMALNY = config", blisko(i16(h[1]) / 10, cf["thrMin"], 0.05), f"{i16(h[1]) / 10} cm")
        ok("102 próg ZA DUŻY = config", blisko(i16(h[2]) / 10, cf["thrHigh"], 0.05), f"{i16(h[2]) / 10} cm")
        ok("103 histereza", blisko(i16(h[3]) / 10, cf["hyst"], 0.05), f"{i16(h[3]) / 10} cm")
        ok("104 opóźnienie", h[4] == cf["delay"], f"{h[4]} s")
        ok("105 wysokość", blisko(i16(h[5]) / 10, cf["height"], 0.05), f"{i16(h[5]) / 10} cm")
        ok("106/107 nagły spadek Y / T", blisko(i16(h[6]) / 10, cf["dropCm"], 0.05) and h[7] == cf["dropMin"], f"{i16(h[6]) / 10} cm / {h[7]} min")

def test_protokol(m, host, port):
    print("\n== Protokół: wyjątki, Unit ID, wielu klientów")
    for fc, a, n, kod, opis in [(4, 500, 1, 2, "FC04 adres 500"), (3, 300, 1, 2, "FC03 adres 300"), (4, 100, 200, 3, "FC04 200 rejestrów naraz")]:
        try: m._regs(fc, a, n); ok(f"{opis} → wyjątek", False, "brak wyjątku")
        except ModbusError as e: ok(f"{opis} → wyjątek {kod:02d}", e.code in (kod, 2, 3), str(e))
    try: m.req(bytes([0x2B, 0x0E, 0x01, 0x00])); ok("funkcja 2B (nieobsługiwana) → wyjątek 01", False, "brak wyjątku")
    except ModbusError as e: ok("funkcja 2B (nieobsługiwana) → wyjątek 01", e.code == 1, str(e))
    for u in (0, 1, 255):
        try: ok(f"Unit ID {u} odpowiada", len(m.input_regs(0, 1, unit=u)) == 1)
        except Exception as e: ok(f"Unit ID {u} odpowiada", False, str(e))
    m2 = None
    try:
        m2 = Modbus(host, port, timeout=1.5); m2.input_regs(0, 1, unit=7); ok("Unit ID 7 – brak odpowiedzi albo wyjątek", False, "odpowiedział")
    except Exception as e: ok("Unit ID 7 – brak odpowiedzi albo wyjątek", True, type(e).__name__)
    finally:
        if m2: m2.close()
    time.sleep(1.5)                                       # sterownik zwalnia miejsce po zamknietym polaczeniu
    kl = []
    try:
        for _ in range(3): kl.append(Modbus(host, port, timeout=3))   # + polaczenie glowne = 4
        wyn = [k.input_regs(0, 1)[0] for k in kl] + [m.input_regs(0, 1)[0]]
        ok("4 klientów jednocześnie (limit sterownika)", len(wyn) == 4, f"{wyn}")
        try:
            k5 = Modbus(host, port, timeout=2); kl.append(k5); k5.input_regs(0, 1)
            ok("5. klient odrzucony (limit 4)", False, "piąty obsłużony")
        except Exception as e: ok("5. klient odrzucony (limit 4)", True, type(e).__name__)
    except Exception as e: ok("4 klientów jednocześnie (limit sterownika)", False, str(e))
    finally:
        for k in kl:
            try: k.close()
            except Exception: pass
    time.sleep(1.5)
    t0 = time.time(); n = 50
    for _ in range(n): m.input_regs(100, 14)
    dt = (time.time() - t0) / n * 1000
    ok("czas odpowiedzi (50 × FC04 14 rej.)", dt < 300, f"średnio {dt:.0f} ms (przez Access Point)")

def test_zapis_zablokowany(m):
    print("\n== Zapis przy wyłączonym zapisie")
    try: m.write_reg(104, m.holding(104, 1)[0]); ok("FC06 → wyjątek 01", False, "zapis przyjęty")
    except ModbusError as e: ok("FC06 → wyjątek 01 (zapis wyłączony)", e.code == 1, str(e))
    try: m.write_coil(100, True); ok("FC05 cewka 100 → wyjątek 01", False, "zapis przyjęty")
    except ModbusError as e: ok("FC05 cewka 100 → wyjątek 01", e.code == 1, str(e))

def test_zapis(m, api):
    print("\n== Zapis (włączony na czas testu przez API)")
    cfg0 = api.config(); t0 = cfg0["tanks"][0]; mb0 = cfg0["modbus"]
    api.set_config({"modbus": {"write": True}})
    try:
        d0 = m.holding(104, 1)[0]
        m.write_reg(104, d0 + 1); time.sleep(0.3)
        ok("FC06 104 opóźnienie +1 s", m.holding(104, 1)[0] == d0 + 1)
        m.write_reg(104, d0); ok("FC06 104 przywrócone", m.holding(104, 1)[0] == d0)
        h = m.holding(100, 3)
        try: m.write_reg(100, h[1] + 10); ok("FC06 KRYTYCZNY > MINIMALNY → wyjątek 03", False, "przyjęty")
        except ModbusError as e: ok("FC06 KRYTYCZNY > MINIMALNY → wyjątek 03", e.code == 3, str(e))
        ok("progi bez zmian po odrzuceniu", m.holding(100, 3) == h)
        nowy = h[1] - 10
        m.write_regs(101, [nowy]); time.sleep(0.5)
        cf = api.config()["tanks"][0]
        ok("FC16 101 MINIMALNY −1 cm", m.holding(101, 1)[0] == nowy, f"{i16(nowy) / 10} cm")
        ok("zapis progu w cm → tryb własny, % przeliczone", cf["alarmMode"] == 0 and blisko(cf["pct"][1], i16(nowy) / 10 / cf["height"] * 100, 0.1), f"tryb {cf['alarmMode']}, % {cf['pct']}")
        try: m.write_coil(0, True); ok("FC05 cewka 0 (wyjście stałe) → wyjątek", False, "przyjęty")
        except ModbusError as e: ok("FC05 cewka 0 (wyjścia stałe) → wyjątek", True, str(e))
    finally:
        api.set_config({"tanks": [{"alarmMode": t0["alarmMode"], "pct": t0["pct"], "hyst": t0["hyst"], "delay": t0["delay"], "height": t0["height"]}, {}],
                        "modbus": {"write": mb0["write"]}})
        cf = api.config()
        ok("przywrócono progi i blokadę zapisu", cf["tanks"][0]["pct"] == t0["pct"] and cf["modbus"]["write"] == mb0["write"], f"% {cf['tanks'][0]['pct']}, zapis {cf['modbus']['write']}")

def czekaj_stan(m, stan, s=40):
    t0 = time.time()
    while time.time() - t0 < s:
        if m.input_regs(103, 1)[0] == stan: return True
        time.sleep(1)
    return False

def test_dynamika(m, api):
    print("\n== Dynamika: zadajnik → Modbus (alarm spadku wyłączony na czas testu)")
    cf = api.config()["tanks"][0]; st0 = api.status()["tanks"][0]
    ma0 = st0["mA"]; drop0 = cf["dropEn"]
    lvl = lambda cm: cf["minX"] + (cm - cf["offset"]) * cf["density"] / 1000 * (cf["maxX"] - cf["minX"]) / (cf["maxCm"] - cf["minCm"])
    api.set_config({"tanks": [{"dropEn": False}, {}]})
    try:
        ma_min = round(lvl((cf["thrCrit"] + cf["thrMin"]) / 2), 2); ma_kr = round(lvl(cf["thrCrit"] / 2), 2)
        gen(ma_min); ok(f"MINIMALNY ({ma_min} mA): rejestr 103 = 1", czekaj_stan(m, 1), f"FC02 11 = {m.discrete(11, 1)[0]}")
        gen(ma_kr); ok(f"KRYTYCZNY ({ma_kr} mA): rejestr 103 = 2", czekaj_stan(m, 2))
        d = m.discrete(8, 6); s1 = m.input_regs(1, 1)[0]; sa = api.status()
        ok("FC02 8 alarm aktywny, 10 KRYTYCZNY", d[0] == 1 and d[2] == 1, f"FC02[8..13]={d}, API stan {sa['tanks'][0]['state']} alarm {sa['alarm']['active']}")
        ok("FC04 1 bit 7 (alarm) ustawiony", bool(s1 & 128), f"status 0b{s1:010b}")
        api.set_config({"modbus": {"write": True}})
        m.write_coil(100, True); time.sleep(1.5)
        ok("FC05 cewka 100 kasuje alarm (FC02 9 skasowany)", m.discrete(9, 1)[0] == 1 and not (api.status()["out"] & 8), f"out {api.status()['out']}")
        api.set_config({"modbus": {"write": False}})
    finally:
        gen(ma0); czekaj_stan(m, 0, 40)
        ok(f"powrót {ma0} mA: stan POPRAWNY", m.input_regs(103, 1)[0] == 0)
        api.set_config({"tanks": [{"dropEn": drop0}, {}], "modbus": {"write": False}})
        if drop0: print("  (alarm spadku włączony z powrotem – spadek z testu może zostać wykryty; wtedy: dropen.py 0, 11 min, dropen.py 1)")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1"); ap.add_argument("--port", type=int, default=502)
    ap.add_argument("--zapis", action="store_true"); ap.add_argument("--dynamika", action="store_true")
    ap.add_argument("--bez-api", action="store_true")
    a = ap.parse_args()
    api = None if a.bez_api else Api()
    print(f"Modbus TCP {a.host}:{a.port} – {time.strftime('%Y-%m-%d %H:%M:%S')}")
    m = Modbus(a.host, a.port)
    try:
        test_odczyty(m, api)
        test_protokol(m, a.host, a.port)
        if api and api.config()["modbus"]["write"] is False: test_zapis_zablokowany(m)
        if a.zapis and api: test_zapis(m, api)
        if a.dynamika and api: test_dynamika(m, api)
    finally:
        m.close()
        if api: api.close()
    n = sum(1 for _, w in WYN if w)
    print(f"\nPODSUMOWANIE: {n}/{len(WYN)} OK" + ("" if n == len(WYN) else "  – BŁĘDY: " + "; ".join(x for x, w in WYN if not w)))
    sys.exit(0 if n == len(WYN) else 1)

if __name__ == "__main__":
    main()
