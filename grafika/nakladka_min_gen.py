#!/usr/bin/env python3
"""Nakładka A4 – wersja MINIMALNA (białe tło do druku, tylko to, co potrzebne przy drzwiach obudowy).
Otwory w tych samych miejscach co nakladka_gen.py, więc obie wersje pasują do tych samych drzwi."""
import pathlib, subprocess

OUT = pathlib.Path(__file__).parent
HOLE_D = 22.3
LAMPS_Y = 120
LAMPS_X = (50, 105, 160)
BTN = (52, 222)
INK, MUTED, LINE, ACC = "#111827", "#6b7280", "#d1d5db", "#0284c7"
GREEN, AMBER, RED = "#16a34a", "#d97706", "#dc2626"
F = "font-family='DejaVu Sans, sans-serif'"


def text(x, y, t, size, fill=INK, weight="normal", anchor="middle"):
    return f"<text x='{x}' y='{y}' {F} font-size='{size}' font-weight='{weight}' fill='{fill}' text-anchor='{anchor}'>{t}</text>"


def hole(cx, cy, col):
    r = HOLE_D / 2
    return (f"<circle cx='{cx}' cy='{cy}' r='{r + 3.5}' fill='none' stroke='{col}' stroke-width='2.5'/>"
            f"<circle cx='{cx}' cy='{cy}' r='{r}' fill='none' stroke='{MUTED}' stroke-width='.25' stroke-dasharray='1.2 .8'/>"
            f"<path d='M{cx - 2} {cy}h4M{cx} {cy - 2}v4' stroke='{MUTED}' stroke-width='.25'/>")


svg = ["<svg xmlns='http://www.w3.org/2000/svg' width='210mm' height='297mm' viewBox='0 0 210 297'>",
       "<rect width='210' height='297' fill='#fff'/>",
       text(105, 70, f"MonitorUp<tspan fill='{ACC}'>.pl</tspan>", 9, INK, "bold"),
       text(105, 78, "Poziom zbiornika", 4, MUTED)]

for cx, (col, lab) in zip(LAMPS_X, [(GREEN, "POPRAWNY"), (AMBER, "MINIMALNY"), (RED, "KRYTYCZNY")]):
    svg += [hole(cx, LAMPS_Y, col), text(cx, LAMPS_Y + 22, lab, 5, col, "bold")]

# czerwona kontrolka i buzer: rysunek 1 sekundy (pasek = świeci)
y0, TX, TW = 160, 40, 20
svg.append(text(TX, y0, "CZERWONA KONTROLKA I BUZER", 3.6, RED, "bold", "start"))
for k, (n, what) in enumerate([(0, "poziom krytyczny"), (1, "przepełnienie"),
                               (2, "nagły spadek (wyciek?)"), (3, "usterka czujnika – serwis")]):
    y = y0 + 9 + k * 7
    svg.append(f"<rect x='{TX}' y='{y - 2.6}' width='{TW}' height='2.6' rx='1.3' fill='#f3f4f6'/>")
    segs = [(0, TW)] if n == 0 else [(j * TW / n, TW / n / 2) for j in range(n)]
    svg += [f"<rect x='{TX + a:.2f}' y='{y - 2.6}' width='{b:.2f}' height='2.6' rx='1.2' fill='{RED}'/>" for a, b in segs]
    how = "ciągle" if n == 0 else f"{n}× / s"
    svg.append(text(TX + TW + 5, y, f"<tspan font-weight='bold'>{how}</tspan>  {what}", 3.8, INK, anchor="start"))

bx, by = BTN
svg += [hole(bx, by, ACC),
        text(80, by - 1, "WYCISZ", 6, ACC, "bold", "start"),
        text(80, by + 6, "buzer i kasowanie alarmu", 3.8, MUTED, anchor="start"),
        text(22, 282, "Serwis, tel.:", 3.6, MUTED, anchor="start"),
        f"<line x1='46' y1='282.5' x2='120' y2='282.5' stroke='{LINE}' stroke-width='.4'/>",
        "</svg>"]

s = "\n".join(svg)
(OUT / "nakladka_A4_min.svg").write_text(s)
html = OUT / "_nakladka_min.html"
html.write_text("<!doctype html><meta charset='utf-8'><style>@page{size:210mm 297mm;margin:0}"
                "html,body{margin:0}svg{display:block;width:210mm;height:297mm}</style>" + s)
ch = ["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--no-pdf-header-footer"]
subprocess.run(ch + [f"--print-to-pdf={OUT / 'nakladka_A4_min.pdf'}", html.as_uri()], check=True, capture_output=True)
subprocess.run(ch + ["--window-size=794,1123", "--force-device-scale-factor=2",
                     f"--screenshot={OUT / 'nakladka_A4_min.png'}", html.as_uri()], check=True, capture_output=True)
html.unlink()
print("OK")
