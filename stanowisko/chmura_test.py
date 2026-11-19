#!/usr/bin/env python3
"""Test poleceń chmury Zdalio na sterowniku MonitorUp 0.6+ (karta #179) – z konta mostu `bridge` na brokerze Zdalio.

  BRIDGE_PASS=… python3 chmura_test.py 4MN682PJPCX                 # podstawowe: info, log, ack, nieznane, expired, cid, cfg
  python3 chmura_test.py 4MN682PJPCX --reboot                       # + restart z chmury
  python3 chmura_test.py 4MN682PJPCX --ota URL --sha256 H --size N --num 61 --fw 0.6.1-dev [--downgrade]

Hasło konta `bridge`: zmienna BRIDGE_PASS albo plik ~/.config/zdalio/bridge.env (600, linia BRIDGE_PASS=…), inaczej pytanie.
Skrypt potwierdza zdarzenia sterownika (ack) jak most – bufor zdarzeń w sterowniku się opróżnia.
Bez zależności (własny klient MQTT 3.1.1 przez TLS). Certyfikat CA brokera z 02_Monitor_App/cloud_ca.h.
"""
import argparse, getpass, json, os, pathlib, re, socket, ssl, struct, sys, tempfile, time, uuid

ROOT = pathlib.Path(__file__).resolve().parent.parent


class Mqtt:
    def __init__(self, host, port, user, pw, cafile):
        ctx = ssl.create_default_context(cafile=cafile)
        self.s = ctx.wrap_socket(socket.create_connection((host, port), 15), server_hostname=host)
        self.s.settimeout(0.5)
        self.buf, self.pid = b"", 1
        cid = "bridge-test-" + uuid.uuid4().hex[:6]
        vh = b"\x00\x04MQTT\x04\xc2" + struct.pack(">H", 60)
        self._send(0x10, vh + self._s(cid) + self._s(user) + self._s(pw))
        t, b = self._wait(0x20, 10)
        if b[1] != 0:
            raise SystemExit(f"Broker odrzucił logowanie konta {user} (kod {b[1]})")

    @staticmethod
    def _s(x):
        x = x.encode() if isinstance(x, str) else x
        return struct.pack(">H", len(x)) + x

    def _send(self, t, body):
        n, enc = len(body), b""
        while True:
            d, n = n % 128, n // 128
            enc += bytes([d | (0x80 if n else 0)])
            if not n:
                break
        self.s.sendall(bytes([t]) + enc + body)

    def _read(self):
        try:
            d = self.s.recv(65536)
            if not d:
                raise SystemExit("Broker zamknął połączenie")
            self.buf += d
        except (socket.timeout, ssl.SSLWantReadError):
            pass
        out = []
        while len(self.buf) >= 2:
            mult, ln, i = 1, 0, 1
            while True:
                if i >= len(self.buf):
                    return out
                c = self.buf[i]; ln += (c & 127) * mult; mult *= 128; i += 1
                if not c & 128:
                    break
            if len(self.buf) < i + ln:
                return out
            out.append((self.buf[0], self.buf[i:i + ln]))
            self.buf = self.buf[i + ln:]
        return out

    def _wait(self, typ, tmo):
        end = time.time() + tmo
        while time.time() < end:
            for t, b in self._read():
                if t & 0xF0 == typ:
                    return t, b
        raise SystemExit("Brak odpowiedzi brokera")

    def sub(self, topic):
        self.pid += 1
        self._send(0x82, struct.pack(">H", self.pid) + self._s(topic) + b"\x01")

    def pub(self, topic, payload, retain=False):
        p = json.dumps(payload, separators=(",", ":")).encode() if not isinstance(payload, bytes) else payload
        self._send(0x30 | (1 if retain else 0), self._s(topic) + p)

    def poll(self):
        msgs = []
        for t, b in self._read():
            if t & 0xF0 != 0x30:
                continue
            tl = struct.unpack(">H", b[:2])[0]
            topic, rest = b[2:2 + tl].decode(), b[2 + tl:]
            if (t >> 1) & 3:                                       # QoS 1: id + PUBACK
                pid, rest = rest[:2], rest[2:]
                self._send(0x40, pid)
            msgs.append((topic, rest))
        return msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hw")
    ap.add_argument("--host", default="cezary173.mikrus.xyz")
    ap.add_argument("--port", type=int, default=40022)
    ap.add_argument("--user", default="bridge")
    ap.add_argument("--reboot", action="store_true")
    ap.add_argument("--ota"); ap.add_argument("--sha256"); ap.add_argument("--size", type=int); ap.add_argument("--num", type=int)
    ap.add_argument("--fw"); ap.add_argument("--downgrade", action="store_true")
    a = ap.parse_args()
    hw = re.sub(r"[^0-9A-Z]", "", a.hw.upper())
    pw = os.environ.get("BRIDGE_PASS")
    envf = pathlib.Path("~/.config/zdalio/bridge.env").expanduser()
    if not pw and envf.exists():
        m = re.search(r"^BRIDGE_PASS=(.*)$", envf.read_text(), re.M)
        pw = m and m.group(1).strip().strip('"')
    pw = pw or getpass.getpass(f"Hasło konta {a.user}: ")
    pem = re.search(r'R"PEM\((.*?)\)PEM"', (ROOT / "02_Monitor_App/cloud_ca.h").read_text(), re.S).group(1)
    caf = tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False); caf.write(pem); caf.close()
    m = Mqtt(a.host, a.port, a.user, pw, caf.name)
    os.unlink(caf.name)
    base = f"e-tank/{hw}/v1/"
    m.sub(base + "#")
    state = {"res": {}, "evt": [], "info": None, "status": None}

    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            for t, p in m.poll():
                sfx = t[len(base):]
                try:
                    d = json.loads(p)
                except ValueError:
                    continue
                if sfx == "status":
                    state["status"] = d
                elif sfx == "info":
                    state["info"] = d
                elif sfx == "res":
                    state["res"].setdefault(d.get("cid"), []).append(d)
                elif sfx.startswith("evt/"):
                    state["evt"].append((sfx, d))
                    m.pub(base + "ack", {"boot": d.get("boot"), "seq": [d.get("seq")]})

    def cmd(name, data=None, exp=120, cid=None, wait=15, want=None):
        cid = cid or uuid.uuid4().hex[:12]
        m.pub(base + "cmd/" + name, {"v": 1, "cid": cid, "exp": int(time.time()) + exp if exp else 0, "data": data or {}})
        end = time.time() + wait
        while time.time() < end and not any(r.get("st") != "busy" for r in state["res"].get(cid, [])):
            pump(0.5)
        r = [x for x in state["res"].get(cid, []) if x.get("st") != "busy"]
        return cid, (r[-1] if r else None)

    wyniki = []

    def check(nazwa, ok, info=""):
        wyniki.append((nazwa, ok, info))
        print(f"{'OK  ' if ok else 'BŁĄD'} {nazwa} {info}", flush=True)

    pump(5)
    check("status online (retained)", (state["status"] or {}).get("st") == "online", json.dumps(state["status"]))
    check("info (retained)", bool(state["info"]), (state["info"] or {}).get("data", {}).get("fw", ""))
    c, r = cmd("sys/info"); check("sys/info", r and r["st"] == "ok")
    c, r = cmd("sys/log", {"n": 20}, wait=25); pump(3)
    parts = [e for s, e in state["evt"] if s == "evt/log" and e.get("data", {}).get("msg") == c]
    check("sys/log + evt/log", r and r["st"] == "ok" and len(parts) == (r.get("data") or {}).get("parts"), f"części {len(parts)}")
    c, r = cmd("tank/ack"); check("tank/ack", r and r["st"] == "ok", json.dumps((r or {}).get("data")))
    c, r = cmd("tank/out", {"k": 1, "v": 100}); check("tank/out (zakazane) → unsupported", r and r["st"] == "unsupported")
    c, r = cmd("sys/reboot", exp=-60); check("przeterminowane → expired", r and r["st"] == "expired")
    c, r = cmd("sys/reboot", exp=0); check("restart bez exp → expired", r and r["st"] == "expired")
    c1, r1 = cmd("sys/info")
    c2, r2 = cmd("sys/info", cid=c1)
    check("powtórzony cid → ten sam wynik", r1 and len(state["res"].get(c1, [])) >= 2)
    rev = int(time.time())
    m.pub(base + "cfg", {"rev": rev, "data": {"wifi": {"ssid": "x"}}}, retain=True); pump(8)
    rej = [e for s, e in state["evt"] if s == "evt/sys" and e.get("data", {}).get("cfg") == "rejected" and e["data"].get("rev") == rev]
    check("cfg spoza listy → rejected", bool(rej))
    rev += 1
    tele = (state["info"] or {}).get("data", {}).get("tele_s", 60)
    m.pub(base + "cfg", {"rev": rev, "data": {"tele_s": tele}}, retain=True); pump(10)
    check("cfg dozwolone → info.cfg_rev", (state["info"] or {}).get("data", {}).get("cfg_rev") == rev)
    m.pub(base + "cfg", b"", retain=True)                            # usuniecie retained cfg (test)
    if a.ota:
        c, r = cmd("sys/ota", {"url": a.ota, "sha256": a.sha256, "size": a.size, "num": a.num, "fw": a.fw, "downgrade": a.downgrade}, wait=5)
        print("OTA: polecenie wysłane, czekam na wynik po restarcie (do 15 min)…", flush=True)
        end = time.time() + 900
        while time.time() < end and not r:
            pump(2)
            rr = [x for x in state["res"].get(c, []) if x.get("st") != "busy"]
            r = rr[-1] if rr else None
        check("sys/ota", r and r["st"] == "ok", json.dumps(r))
    if a.reboot:
        boot = (state["status"] or {}).get("boot")
        c, r = cmd("sys/reboot"); check("sys/reboot → ok", r and r["st"] == "ok")
        end = time.time() + 120
        while time.time() < end and (state["status"] or {}).get("boot") in (boot, None) or (state["status"] or {}).get("st") != "online":
            pump(1)
            if time.time() > end:
                break
        check("ponowne połączenie po restarcie (nowy boot)", (state["status"] or {}).get("boot") not in (boot, None))
    zle = [w for w in wyniki if not w[1]]
    print(f"\nWYNIK: {len(wyniki) - len(zle)}/{len(wyniki)} OK; zdarzeń odebranych: {len(state['evt'])}")
    return 1 if zle else 0


if __name__ == "__main__":
    sys.exit(main())
