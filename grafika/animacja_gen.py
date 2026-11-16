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
STATES = [("Stan normalny", "praca bez zakłóceń", GRN[0], (1, 0, 0)),
          ("Poziom minimalny", "zaplanować uzupełnienie", AMB[0], (0, 1, 0)),
          ("Awaria czujnika", "wezwać serwis", AMB[0], (0, 2, 0)),
          ("Poziom krytyczny", "uzupełnić zbiornik", RED[0], (0, 0, 1)),
          ("Przepełnienie", "wstrzymać napełnianie", RED[0], (0, 0, 2)),
          ("Nagły spadek poziomu", "sprawdzić szczelność", RED[0], (0, 0, 4))]
FPS, LOOP = 20, 2.0
W, H, PW, PH = 960, 790, 300, 345
# poziom w zbiorniku dla stanu (0–1); None = nieznany (awaria czujnika), 'spadek' = animowany spadek
LEVEL = [0.60, 0.22, None, 0.10, 0.96, 'spadek']
ZONES = [(0.00, 0.12, RED[0]), (0.12, 0.30, AMB[0]), (0.30, 0.90, GRN[0]), (0.90, 1.00, RED[0])]


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
    d.text((30, 18), "MonitorUp.pl – sygnalizacja stanów", font=f_t, fill=TXT)
    for k, (title, todo, tcol, modes) in enumerate(STATES):
        x0, y0 = 20 + (k % 3) * (PW + 10), 70 + (k // 3) * (PH + 15)
        d.rounded_rectangle((x0, y0, x0 + PW, y0 + PH), 12, fill=PANEL, outline=LINE, width=2)
        for j, ((col, dim), lab, m) in enumerate(zip((GRN, AMB, RED), ("NORMA", "OSTRZEŻENIE", "ALARM"), modes)):
            cx, cy = x0 + 55 + j * 95, y0 + 75
            lit = on(m, t)
            if lit:
                d.ellipse((cx - 40, cy - 40, cx + 40, cy + 40), fill=dim)
            d.ellipse((cx - 30, cy - 30, cx + 30, cy + 30), fill=col if lit else "#070c18", outline=col, width=3)
            d.text((cx, cy + 50), lab, font=f_l, fill=col, anchor="mm")
        # zbiornik: strefy jako pasek z boku, wypełnienie w kolorze stanu
        tx, ty, tw, th = x0 + PW / 2 - 60, y0 + 158, 120, 62
        r = th // 3
        d.rounded_rectangle((tx, ty, tx + tw, ty + th), r, fill="#070c18")
        for lo, hi, zc in ZONES:
            d.rectangle((tx + tw + 8, ty + th * (1 - hi), tx + tw + 14, ty + th * (1 - lo)), fill=zc)
        lv = LEVEL[k]
        if lv == "spadek":
            lv = 0.62 - 0.30 * ((t % LOOP) / LOOP)
        if lv is None:
            d.text((tx + tw / 2, ty + th / 2), "?", font=f_t, fill=MUTED, anchor="mm")
        else:
            fill = Image.new("RGB", (tw, th), tcol)
            mask = Image.new("L", (tw, th), 0)
            md = ImageDraw.Draw(mask)
            md.rounded_rectangle((0, 0, tw - 1, th - 1), r, fill=255)
            md.rectangle((0, 0, tw, int(th * (1 - lv))), fill=0)
            im.paste(fill, (int(tx), int(ty)), mask)
            if LEVEL[k] == "spadek":
                ax = tx - 18
                d.line((ax, ty + 8, ax, ty + 48), fill=tcol, width=3)
                d.polygon([(ax - 7, ty + 42), (ax + 7, ty + 42), (ax, ty + 54)], fill=tcol)
        d.rounded_rectangle((tx, ty, tx + tw, ty + th), r, outline=MUTED, width=2)
        for sx in (tx + 22, tx + tw - 34):
            d.polygon([(sx, ty + th - 2), (sx + 12, ty + th - 2), (sx + 16, ty + th + 10), (sx - 4, ty + th + 10)],
                      fill="#1c2942", outline=MUTED)
        d.text((x0 + PW / 2, y0 + 262), title, font=f_b, fill=tcol, anchor="mm")
        d.text((x0 + PW / 2, y0 + 290), todo, font=f_s, fill=TXT, anchor="mm")
        how = {(1, 0, 0): "NORMA – światło ciągłe", (0, 1, 0): "OSTRZEŻENIE – światło ciągłe",
               (0, 2, 0): "OSTRZEŻENIE – przerywane", (0, 0, 1): "ALARM – światło ciągłe",
               (0, 0, 2): "ALARM – przerywane", (0, 0, 4): "ALARM – przerywane szybkie"}[modes]
        d.text((x0 + PW / 2, y0 + 322), how, font=f_s, fill=MUTED, anchor="mm")
    frames.append(im)

frames[0].save(OUT / "animacja_stanow.gif", save_all=True, append_images=frames[1:],
               duration=1000 // FPS, loop=0, optimize=True)
frames[0].save(OUT / "animacja_stanow_klatka.png")
print("OK")
