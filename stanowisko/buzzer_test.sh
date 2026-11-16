#!/bin/bash
# Po ponownym podlaczeniu A4: rytm kontrolki i buzera dla KRYTYCZNY / ZA DUZY / NAGLY SPADEK (SW 0.5.2)
cd ~
echo "[$(date +%T)] czekam na podłączenie A4…"
for i in $(seq 1 180); do s=$(cd ~/rpi && python3 -c "
from masim.config import load
from masim.board import Board
c=load(); b=Board(c['plytka']['adres'], c['sekrety']['token'], 8)
try: print(b.status()['tanks'][0]['state'])
except Exception: print(4)" 2>/dev/null); [ "$s" != "4" ] && break; sleep 5; done
echo "[$(date +%T)] A4 podłączone (stan $s)"; sleep 10
python3 dropen.py 0
echo "=== KRYTYCZNY (5.2 mA)"; python3 sym.py 5.2 25 | grep -v INFO | head -3; python3 blink.py 6
echo "=== ZA DUŻY (19.8 mA)"; python3 sym.py 19.8 25 | grep -v INFO | head -3; python3 blink.py 6
python3 sym.py ack 2 | head -1
python3 dropen.py 1
echo "=== NAGŁY SPADEK: 14 mA, czekanie na koniec tankowania, potem 12.8 mA"
bash droptest.sh | grep -v INFO
python3 dropen.py 0; python3 dropen.py 1; python3 sym.py 14 25 | grep -v INFO | head -3
