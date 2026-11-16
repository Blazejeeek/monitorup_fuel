#!/usr/bin/env python3
"""Nakładka A4 na drzwi obudowy MonitorUp.pl – SVG 1:1 w mm, PDF i PNG przez Chromium.
Wymiary do zmiany poniżej (otwory 22 mm = kontrolki/przycisk fi 22, otwór 22,3 mm)."""
import subprocess, pathlib, sys
OUT = pathlib.Path(__file__).parent
HOLE_D = 22.3           # średnica otworu [mm]
LAMPS_Y = 120           # oś kontrolek [mm od góry arkusza]
LAMPS_X = (50, 105, 160)
BTN = (52, 222)         # przycisk wyciszenia (x, y)
CUT = (0, 0, 210, 297)  # tło na całą stronę A4 (bez białych marginesów)

NAVY, PANEL, LINE, TXT, MUTED, ACC = "#0b1324", "#111c31", "#24324d", "#e8eef8", "#93a4bf", "#38bdf8"
F = "font-family='DejaVu Sans, sans-serif'"

def hole(cx, cy, ring, label_ring=True):
    r = HOLE_D / 2
    s = []
    if label_ring:
        s.append(f"<circle cx='{cx}' cy='{cy}' r='{r+5.5}' fill='none' stroke='{ring}' stroke-width='2.2'/>")
        s.append(f"<circle cx='{cx}' cy='{cy}' r='{r+2.2}' fill='{ring}' opacity='.18'/>")
    s.append(f"<circle cx='{cx}' cy='{cy}' r='{r}' fill='#060b16' stroke='#93a4bf' stroke-width='.3' stroke-dasharray='1.2 .8'/>")
    s.append(f"<path d='M{cx-3} {cy}h6M{cx} {cy-3}v6' stroke='#93a4bf' stroke-width='.3'/>")
    s.append(f"<text x='{cx}' y='{cy+7}' {F} font-size='2.2' fill='#5b6b86' text-anchor='middle'>⌀{str(HOLE_D).replace('.',',')}</text>")
    return "\n".join(s)

def text(x, y, t, size, fill=TXT, weight="normal", anchor="middle", ls=0):
    return f"<text x='{x}' y='{y}' {F} font-size='{size}' font-weight='{weight}' fill='{fill}' text-anchor='{anchor}' letter-spacing='{ls}'>{t}</text>"

x0, y0, w, h = CUT
svg = [f"<svg xmlns='http://www.w3.org/2000/svg' width='210mm' height='297mm' viewBox='0 0 210 297'>",
       "<defs><linearGradient id='g' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#38bdf8'/><stop offset='1' stop-color='#0369a1'/></linearGradient>"
       "<clipPath id='c'><rect x='12' y='8' width='40' height='48' rx='7'/></clipPath>"
       f"<clipPath id='panel'><rect x='{x0}' y='{y0}' width='{w}' height='{h}'/></clipPath></defs>",
       f"<rect x='{x0}' y='{y0}' width='{w}' height='{h}' fill='{NAVY}'/>",
       # pasek nagłówka
       f"<g clip-path='url(#panel)'><rect x='{x0}' y='{y0}' width='{w}' height='64' fill='{PANEL}'/>"
       f"<path d='M{x0} 62 Q 40 56 70 62 T 140 62 T 210 62 V66 H{x0}Z' fill='url(#g)' opacity='.9'/></g>",
       # logo (to samo co na stronie urządzenia)
       "<g transform='translate(20 17) scale(.56)'>"
       "<rect x='12' y='8' width='40' height='48' rx='7' fill='#0b1324' stroke='#38bdf8' stroke-width='3'/>"
       "<g clip-path='url(#c)'><path d='M0 30 Q8 25 16 30 T32 30 T48 30 T64 30 V64 H0Z' fill='url(#g)'/>"
       "<path d='M0 34 Q8 30 16 34 T32 34 T48 34 T64 34 V64 H0Z' fill='#0ea5e9' opacity='.55'/></g>"
       "<path d='M20 20h8M20 26h5' stroke='#38bdf8' stroke-width='2.5' stroke-linecap='round'/></g>",
       f"<text x='60' y='41' {F} font-size='15' font-weight='bold' fill='{TXT}' letter-spacing='.3'>MonitorUp<tspan fill='{ACC}'>.pl</tspan></text>",
       # nazwa zbiornika
       text(22, 82, "ZBIORNIK", 3.4, MUTED, "bold", "start", .6),
       f"<line x1='48' y1='82.5' x2='188' y2='82.5' stroke='{LINE}' stroke-width='.5'/>",
]
RED, GRN, AMB = "#ef4444", "#22c55e", "#f59e0b"
# Propozycja „kolor mówi, co zrobić”: zielona = OK, pomarańczowa = uwaga/serwis, czerwona = działaj
lamps = [(GRN, "NORMA"), (AMB, "OSTRZEŻENIE"), (RED, "ALARM")]
for cx, (col, lab) in zip(LAMPS_X, lamps):
    svg.append(hole(cx, LAMPS_Y, col))
    svg.append(text(cx, LAMPS_Y + 27, lab, 5.2, col, "bold", ls=.6))

# legenda: pasek ciągły = świeci, długie kreski = miga, krótkie kropki = miga szybko
yl = 152
svg += [f"<rect x='26' y='{yl}' width='158' height='46' rx='4' fill='{PANEL}' stroke='{LINE}' stroke-width='.4'/>"]
rows = [(AMB, "s", "Poziom minimalny", "zaplanować uzupełnienie"),
        (AMB, "m", "Awaria czujnika", "wezwać serwis"),
        (RED, "s", "Poziom krytyczny", "uzupełnić zbiornik"),
        (RED, "m", "Przepełnienie", "wstrzymać napełnianie"),
        (RED, "f", "Nagły spadek poziomu", "sprawdzić szczelność")]
TX, TW = 33, 14
for k, (col, kind, what, todo) in enumerate(rows):
    y = yl + 9 + k * 8
    svg.append(f"<rect x='{TX}' y='{y-2.8}' width='{TW}' height='2.8' rx='1.4' fill='#1c2942'/>")
    if kind == "s":
        segs = [(0, TW)]
    elif kind == "m":
        segs = [(j * 5, 2.6) for j in range(3)]
    else:
        segs = [(j * 2.4, 1.3) for j in range(6)]
    svg += [f"<rect x='{TX + a:.2f}' y='{y-2.8}' width='{b:.2f}' height='2.8' rx='1.3' fill='{col}'/>" for a, b in segs]
    svg.append(text(TX + TW + 3, y, {"s": "ciągłe", "m": "przerywane", "f": "szybkie"}[kind], 3.2, MUTED, anchor="start"))
    svg.append(text(TX + TW + 26, y, f"<tspan font-weight='bold' fill='{col}'>{what}</tspan>  –  {todo}", 4.2, TXT, anchor="start"))

# przycisk wyciszenia
bx, by = BTN
svg += [hole(bx, by, ACC),
        text(bx, by + 24, "KASOWANIE", 4.6, ACC, "bold", ls=.4),
        text(bx, by + 29.5, "ALARMU", 4.6, ACC, "bold", ls=.4)]

# zbiornik ze strefami w kolorach kontrolek (od dołu: krytyczny, minimalny, norma, przepełnienie)
TKX, TKY, TKW, TKH = 104, 207, 30, 64
zones = [(0.00, 0.12, RED, "POZIOM KRYTYCZNY"), (0.12, 0.30, AMB, "POZIOM MINIMALNY"),
         (0.30, 0.90, GRN, "NORMA"), (0.90, 1.00, RED, "PRZEPEŁNIENIE")]
svg.append(f"<clipPath id='tk'><rect x='{TKX}' y='{TKY}' width='{TKW}' height='{TKH}' rx='5'/></clipPath>")
svg.append(f"<rect x='{TKX}' y='{TKY}' width='{TKW}' height='{TKH}' rx='5' fill='#060b16'/>")
svg.append("<g clip-path='url(#tk)'>")
for lo, hi, col, lab in zones:
    y1, y2 = TKY + TKH * (1 - hi), TKY + TKH * (1 - lo)
    svg.append(f"<rect x='{TKX}' y='{y1:.2f}' width='{TKW}' height='{y2 - y1:.2f}' fill='{col}' opacity='.75'/>")
    if lo > 0:
        svg.append(f"<line x1='{TKX}' y1='{y2:.2f}' x2='{TKX + TKW}' y2='{y2:.2f}' stroke='#0b1324' stroke-width='.6'/>")
svg.append("</g>")
svg.append(f"<rect x='{TKX}' y='{TKY}' width='{TKW}' height='{TKH}' rx='5' fill='none' stroke='{MUTED}' stroke-width='.6'/>")
for lo, hi, col, lab in zones:
    ym = TKY + TKH * (1 - (lo + hi) / 2)
    svg.append(f"<line x1='{TKX + TKW + 1.5}' y1='{ym:.2f}' x2='{TKX + TKW + 5}' y2='{ym:.2f}' stroke='{col}' stroke-width='.5'/>")
    svg.append(text(TKX + TKW + 7, ym + 1.3, lab, 3.6, col, "bold", "start", .3))
# stopka
yf = 281
svg += [f"<line x1='22' y1='{yf-2}' x2='188' y2='{yf-2}' stroke='{LINE}' stroke-width='.4'/>",
        text(105, yf + 4, "www.monitorup.pl", 4.4, ACC, "bold"),
        "</svg>"]
s = "\n".join(svg)
(OUT / "nakladka_A4.svg").write_text(s)
html = OUT / "_nakladka.html"
html.write_text("<!doctype html><meta charset='utf-8'><style>@page{size:210mm 297mm;margin:0}html,body{margin:0;background:#0b1324;-webkit-print-color-adjust:exact;print-color-adjust:exact}svg{display:block;width:210mm;height:297mm}</style>" + s)
ch = ["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--no-pdf-header-footer"]
subprocess.run(ch + [f"--print-to-pdf={OUT/'nakladka_A4.pdf'}", html.as_uri()], check=True, capture_output=True)
subprocess.run(ch + ["--window-size=794,1123", "--force-device-scale-factor=2", f"--screenshot={OUT/'nakladka_A4.png'}", html.as_uri()], check=True, capture_output=True)
html.unlink()
print("OK")
