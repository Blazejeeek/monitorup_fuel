#!/usr/bin/env python3
"""Podpis pliku aktualizacji MonitorUp.pl: sign_fw.py <wejscie.bin> <wyjscie.bin> [klucz.pem]
Doklejane 72 bajty: podpis ECDSA P-256 (r||s) nad SHA-256 obrazu + "MUPSIG01" (format: 02_Monitor_App/otasig.h).
Klucz prywatny domyslnie ~/.config/monitorup/ota_sign_key.pem (tylko na serwerze budowania, nie w repozytorium).
Sprawdzenie podpisu: sign_fw.py --sprawdz <plik.bin> [klucz_publiczny.pem]"""
import os, subprocess, sys, tempfile, hashlib
MAGIC = b"MUPSIG01"
KEY = os.path.expanduser("~/.config/monitorup/ota_sign_key.pem")
PUB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ota_sign_pub.pem")

def der_to_raw(der):
    assert der[0] == 0x30
    i = 2 if der[1] < 0x80 else 3
    out = b""
    for _ in range(2):
        assert der[i] == 0x02; ln = der[i + 1]; v = der[i + 2:i + 2 + ln]; i += 2 + ln
        out += v.lstrip(b"\0").rjust(32, b"\0")
    return out

def raw_to_der(raw):
    def enc(v):
        v = v.lstrip(b"\0") or b"\0"
        if v[0] & 0x80: v = b"\0" + v
        return b"\x02" + bytes([len(v)]) + v
    body = enc(raw[:32]) + enc(raw[32:])
    return b"\x30" + bytes([len(body)]) + body

def openssl(args, data):
    return subprocess.run(["openssl"] + args, input=data, capture_output=True, check=True).stdout

if len(sys.argv) >= 3 and sys.argv[1] == "--sprawdz":
    blob = open(sys.argv[2], "rb").read(); pub = sys.argv[3] if len(sys.argv) > 3 else PUB
    if blob[-8:] != MAGIC: sys.exit("BRAK PODPISU")
    img, raw = blob[:-72], blob[-72:-8]
    with tempfile.NamedTemporaryFile() as sf:
        sf.write(raw_to_der(raw)); sf.flush()
        r = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sf.name], input=img, capture_output=True)
    print("PODPIS OK" if r.returncode == 0 else "PODPIS NIEPRAWIDŁOWY"); sys.exit(r.returncode)

src, dst = sys.argv[1], sys.argv[2]
key = sys.argv[3] if len(sys.argv) > 3 else KEY
img = open(src, "rb").read()
if img[-8:] == MAGIC: sys.exit("Plik jest już podpisany")
if img[0] != 0xE9: sys.exit("To nie jest obraz programu ESP32 (brak 0xE9 na początku)")
raw = der_to_raw(openssl(["dgst", "-sha256", "-sign", key], img))
open(dst, "wb").write(img + raw + MAGIC)
print(f"Podpisano: {dst} ({len(img)} + 72 B), SHA-256 obrazu {hashlib.sha256(img).hexdigest()[:16]}…")
