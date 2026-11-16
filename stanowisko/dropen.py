#!/usr/bin/env python3
"""dropen.py 0|1 – wylacza/wlacza alarm naglego spadku (wylaczenie gasi zatrzasniety alarm)."""
import sys
sys.path.insert(0, "/home/glimmer/rpi")
from masim.config import load
from masim.board import Board
c = load(); s = c["sekrety"]; b = Board(c["plytka"]["adres"], "", 10); ck = b.login(s["user"], s["pass"])
b._req("POST", "/api/v1/config", {"tanks": [{"dropEn": sys.argv[1] == "1"}, {}]}, cookie=ck)
print("Alarm nagłego spadku:", "WŁĄCZONY" if sys.argv[1] == "1" else "WYŁĄCZONY")
b._req("POST", "/api/v1/logout", {}, cookie=ck)
