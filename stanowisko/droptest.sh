#!/bin/bash
# Test naglego spadku: powrot na 14 mA, czekanie na koniec "tankowania", spadek o 15 cm, pomiar migania, wyciszenie.
cd ~
st() { cd ~/rpi && python3 -c "
from masim.config import load
from masim.board import Board
c=load(); b=Board(c['plytka']['adres'], c['sekrety']['token'], 8)
try:
    t=b.status()['tanks'][0]; print(t['state'], int(t['stats'].get('refilling', False)), t['cm'])
except Exception: print('x 1 0')" 2>/dev/null; }
echo "[$(date +%T)] wyciszenie, alarm spadku WŁĄCZONY, 14 mA"
python3 sym.py ack 2 | grep -v INFO | head -1
python3 dropen.py 1
python3 sym.py 14 25 | grep -v INFO
for i in $(seq 1 150); do r=$(st); set -- $r; [ "$2" = "0" ] && break; sleep 10; done
echo "[$(date +%T)] tankowanie zakończone (stan $1, refilling $2, $3 cm) – kontrola 2 min, czy nie zaczyna się następne"
for i in 1 2 3 4; do sleep 30; r=$(st); echo "  [$(date +%T)] stan/refilling/cm: $r"; done
echo "[$(date +%T)] spadek na 12.8 mA"
python3 sym.py 12.8 5 | grep -v INFO
for i in $(seq 1 30); do r=$(st); set -- $r; [ "$1" = "6" ] && break; sleep 5; done
echo "[$(date +%T)] stan po spadku: $1 ($3 cm)"
python3 sym.py 12.8 1 | grep -v INFO
python3 blink.py 6
echo "[$(date +%T)] wyciszenie"
python3 sym.py ack 3 | grep -v INFO
