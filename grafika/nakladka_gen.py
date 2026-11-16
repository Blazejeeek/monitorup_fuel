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
RED = "#ef4444"
lamps = [("#22c55e", "NORMALNY", []),
         ("#f59e0b", "NIEPRAWIDŁOWY", []),
         (RED, "KRYTYCZNY", [])]
for cx, (col, lab, desc) in zip(LAMPS_X, lamps):
    svg.append(hole(cx, LAMPS_Y, col))
    svg.append(text(cx, LAMPS_Y + 27, lab, 5.2, col, "bold", ls=.4))
    for k, d in enumerate(desc):
        svg.append(text(cx, LAMPS_Y + 33.5 + k * 4.6, d, 3.5, MUTED))

# legenda czerwonej kontrolki: oś = 5 s (pasek = świeci, kropki = mignięcia)
yl = 160
svg += [f"<rect x='40' y='{yl}' width='130' height='30' rx='4' fill='{PANEL}' stroke='{LINE}' stroke-width='.4'/>"]
rows = [(0, "usterka czujnika"),
        (2, "niski poziom"),
        (3, "wysoki poziom")]
TX, TW = 52, 26                       # oś czasu: x, szerokość (= 5 s)
svg.append(text(TX + TW / 2, yl + 5, "5 s", 2.6, MUTED))
for k, (n, what) in enumerate(rows):
    y = yl + 12 + k * 7
    svg.append(f"<rect x='{TX}' y='{y-2.6}' width='{TW}' height='2.6' rx='1.3' fill='#1c2942'/>")
    if n == 0:
        svg.append(f"<rect x='{TX}' y='{y-2.6}' width='{TW}' height='2.6' rx='1.3' fill='{RED}'/>")
    else:
        for j in range(n):
            svg.append(f"<rect x='{TX + 0.5 + j*4.2:.2f}' y='{y-2.6}' width='2.6' height='2.6' rx='1.3' fill='{RED}'/>")
    svg.append(text(TX + TW + 8, y, what, 4, TXT, anchor="start"))

# przycisk wyciszenia
bx, by = BTN
svg += [hole(bx, by, ACC),
        text(84, by + 2.5, "WYCISZ", 7, ACC, "bold", "start", .4),
        f"<g transform='translate(127 {by-4}) scale(1.1)' fill='none' stroke='{ACC}' stroke-width='.7' stroke-linejoin='round' stroke-linecap='round'>"
        "<path d='M0 2h2.5l3-2.5v8l-3-2.5H0z' fill='#38bdf8' fill-opacity='.25'/><path d='M8 0.5l4 4M12 0.5l-4 4'/></g>"]
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
