#!/usr/bin/env python3
"""Animacja GIF wszystkich stanów kontrolek (propozycja „kolor mówi, co zrobić”) → animacja_stanow.gif.
Miga = 1 Hz (0,5 s świeci / 0,5 s zgaszona), miga szybko = 4 Hz. Cykl animacji 2 s."""
import pathlib
from PIL import Image, ImageDraw, ImageFont

OUT = pathlib.Path(__file__).parent
FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
f_t, f_b, f_s, f_l = (ImageFont.truetype(FB, 22), ImageFont.truetype(FB, 20),
                      ImageFont.truetype(FR, 17), ImageFont.truetype(FB, 13))
GRN, AMB, RED = ("#22c55e", "#0f2a1a"), ("#f59e0b", "#2e2210"), ("#ef4444", "#2e1216")
BG, PANEL, LINE, TXT, MUTED = "#0b1324", "#111c31", "#24324d", "#e8eef8", "#93a4bf"
# (tytuł, polecenie, kolor tytułu, stan zielonej/pomarańczowej/czerwonej: 0 zgaszona, 1 świeci, 2 miga, 4 miga szybko)
STATES = [("Wszystko OK", "nic nie rób", GRN[0], (1, 0, 0)),
          ("Niski poziom", "zaplanuj tankowanie", AMB[0], (0, 1, 0)),
          ("Usterka", "wezwij serwis", AMB[0], (0, 2, 0)),
          ("Brak paliwa", "zatankuj", RED[0], (0, 0, 1)),
          ("Przepełnienie", "przerwij tankowanie", RED[0], (0, 0, 2)),
          ("Wyciek?", "sprawdź zbiornik", RED[0], (0, 0, 4))]
FPS, LOOP = 20, 2.0
W, H, PW, PH = 960, 640, 300, 270


def on(mode, t):
    if mode in (0, 1):
        return mode == 1
    period = 1.0 / (1 if mode == 2 else 4)
    return (t % period) < period / 2


frames = []
for i in range(int(FPS * LOOP)):
    t = i / FPS
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.text((30, 18), "MonitorUp – co pokazują kontrolki", font=f_t, fill=TXT)
    for k, (title, todo, tcol, modes) in enumerate(STATES):
        x0, y0 = 20 + (k % 3) * (PW + 10), 70 + (k // 3) * (PH + 15)
        d.rounded_rectangle((x0, y0, x0 + PW, y0 + PH), 12, fill=PANEL, outline=LINE, width=2)
        for j, ((col, dim), lab, m) in enumerate(zip((GRN, AMB, RED), ("OK", "UWAGA", "ALARM"), modes)):
            cx, cy = x0 + 55 + j * 95, y0 + 75
            lit = on(m, t)
            if lit:
                d.ellipse((cx - 40, cy - 40, cx + 40, cy + 40), fill=dim)
            d.ellipse((cx - 30, cy - 30, cx + 30, cy + 30), fill=col if lit else "#070c18", outline=col, width=3)
            d.text((cx, cy + 50), lab, font=f_l, fill=col, anchor="mm")
        d.text((x0 + PW / 2, y0 + 175), title, font=f_b, fill=tcol, anchor="mm")
        d.text((x0 + PW / 2, y0 + 205), todo, font=f_s, fill=TXT, anchor="mm")
        how = {(1, 0, 0): "zielona świeci", (0, 1, 0): "pomarańczowa świeci", (0, 2, 0): "pomarańczowa miga",
               (0, 0, 1): "czerwona świeci", (0, 0, 2): "czerwona miga", (0, 0, 4): "czerwona miga szybko"}[modes]
        d.text((x0 + PW / 2, y0 + 240), how, font=f_s, fill=MUTED, anchor="mm")
    frames.append(im)

frames[0].save(OUT / "animacja_stanow.gif", save_all=True, append_images=frames[1:],
               duration=1000 // FPS, loop=0, optimize=True)
frames[0].save(OUT / "animacja_stanow_klatka.png")
print("OK")
