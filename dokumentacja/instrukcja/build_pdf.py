#!/usr/bin/env python3
"""Dokumenty MonitorUp.pl w stylu firmowym: Markdown → HTML (CSS) → PDF przez Chromium.

  python3 build_pdf.py            # instrukcja + karta katalogowa
  python3 build_pdf.py instrukcja_obslugi.md

Obsługiwany Markdown: #–###, akapity, listy - / 1., tabele |, obrazy ![](plik), **b**, *i*, `kod`, ---.
Pierwszy blok (tytuł, podtytuł pogrubiony, notka kursywą) do pierwszego „---” trafia na stronę tytułową
(instrukcja) albo do pasa nagłówka (karta katalogowa).
"""
import html, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).parent
NAVY, PANEL, ACC, TXT, MUTED = "#0b1324", "#111c31", "#38bdf8", "#1f2937", "#64748b"
DOCS = {
    "instrukcja_obslugi.md": dict(pdf="MonitorUp_instrukcja_obslugi.pdf", kind="cover",
                                  footer="Instrukcja obsługi i montażu"),
    "karta_katalogowa.md": dict(pdf="MonitorUp_karta_katalogowa.pdf", kind="band", footer="Karta katalogowa"),
    "komunikaty_email.md": dict(pdf="MonitorUp_komunikaty_email.pdf", kind="band", footer="Komunikaty e-mail"),
    "analiza_logowanie_chmura.md": dict(pdf="MonitorUp_logowanie_chmura.pdf", kind="band", footer="Logowanie urządzeń do chmury"),
}

LOGO = """<svg xmlns='http://www.w3.org/2000/svg' viewBox='9 5 46 54'>
<defs><linearGradient id='lg' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#38bdf8'/>
<stop offset='1' stop-color='#0369a1'/></linearGradient><clipPath id='lc'><rect x='12' y='8' width='40' height='48' rx='7'/></clipPath></defs>
<rect x='12' y='8' width='40' height='48' rx='7' fill='#0b1324' stroke='#38bdf8' stroke-width='3'/>
<g clip-path='url(#lc)'><path d='M0 30 Q8 25 16 30 T32 30 T48 30 T64 30 V64 H0Z' fill='url(#lg)'/>
<path d='M0 34 Q8 30 16 34 T32 34 T48 34 T64 34 V64 H0Z' fill='#0ea5e9' opacity='.55'/></g>
<path d='M20 20h8M20 26h5' stroke='#38bdf8' stroke-width='2.5' stroke-linecap='round'/></svg>"""
WORD = "<span class='wm'>MonitorUp<b>.pl</b></span>"
WAVE = ("<svg class='wave' viewBox='0 0 210 12' preserveAspectRatio='none'><path d='M0 6 Q 40 0 70 6 T 140 6 T 210 6 V12 H0Z' "
        "fill='url(#wg)'/><defs><linearGradient id='wg' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#38bdf8'/>"
        "<stop offset='1' stop-color='#0369a1'/></linearGradient></defs></svg>")


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", t)
    t = re.sub(r"\[(do (?:uzupełnienia|potwierdzenia)[^\]]*)\]", r"<span class='todo'>\1</span>", t)
    return t


def md_blocks(lines):
    out, i = [], 0
    while i < len(lines):
        ln = lines[i].rstrip()
        if not ln.strip():
            i += 1
            continue
        if ln.strip().startswith("```"):
            i += 1
            code = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(html.escape(lines[i].rstrip()))
                i += 1
            i += 1
            out.append("<pre>" + "\n".join(code) + "</pre>")
            continue
        if ln.startswith("#"):
            n = len(ln) - len(ln.lstrip("#"))
            txt = ln[n:].strip()
            cls = " class='zal'" if n == 2 and txt.startswith("Załącznik") else ""
            out.append(f"<h{n}{cls}>{inline(txt)}</h{n}>")
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[2:]]
            h = "".join(f"<th>{inline(c)}</th>" for c in head)
            b = "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in body)
            out.append(f"<table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table>")
        elif re.match(r"^(\s*)([-*]|\d+\.)\s", ln):
            tag = "ol" if re.match(r"^\s*\d+\.", ln) else "ul"
            items = []
            while i < len(lines) and re.match(r"^(\s*)([-*]|\d+\.)\s", lines[i]):
                items.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i].rstrip()))
                i += 1
                while i < len(lines) and lines[i].startswith("  ") and lines[i].strip() and \
                        not re.match(r"^\s*([-*]|\d+\.)\s", lines[i]):
                    items[-1] += " " + lines[i].strip()
                    i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
        elif m := re.match(r"^!\[(.*?)\]\((.+?)\)\s*$", ln):
            out.append(f"<figure><img src='{m[2]}'><figcaption>{inline(m[1])}</figcaption></figure>")
            i += 1
        elif ln.strip() == "---":
            out.append("<hr>")
            i += 1
        else:
            para = [ln]
            i += 1
            while i < len(lines) and lines[i].strip() and not re.match(r"^(#|\||!\[|---|```|\s*([-*]|\d+\.)\s)", lines[i]):
                para.append(lines[i].rstrip())
                i += 1
            out.append(f"<p>{inline(' '.join(para))}</p>")
    return "\n".join(out)


CSS = f"""
@page {{ size: A4; margin: 20mm 17mm 18mm 17mm;
  @bottom-left {{ content: "MonitorUp.pl  ·  " var(--foot); font: 7.5pt 'DejaVu Sans'; color: {MUTED}; }}
  @bottom-right {{ content: "Strona " counter(page) " z " counter(pages); font: 7.5pt 'DejaVu Sans'; color: {MUTED}; }}
  @top-right {{ content: "www.monitorup.pl"; font: bold 7.5pt 'DejaVu Sans'; color: {ACC}; }}
}}
@page cover {{ margin: 0; @bottom-left {{ content: none }} @bottom-right {{ content: none }} @top-right {{ content: none }} }}
* {{ box-sizing: border-box; }}
html {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
body {{ margin: 0; font: 9.6pt/1.5 'DejaVu Sans', sans-serif; color: {TXT}; }}
.cover {{ page: cover; height: 297mm; width: 210mm; background: {NAVY}; color: #e8eef8; position: relative;
  padding: 38mm 22mm 0; break-after: page; overflow: hidden; }}
.cover .brand {{ display: flex; align-items: center; gap: 9mm; }}
.cover .brand svg {{ width: 34mm; height: 40mm; }}
.wm {{ font-weight: bold; letter-spacing: .3px; }} .wm b {{ color: {ACC}; }}
.cover .wm {{ font-size: 34pt; }}
.cover .wave {{ position: absolute; left: 0; top: 98mm; width: 210mm; height: 14mm; }}
.cover h1 {{ margin: 52mm 0 6mm; font-size: 25pt; line-height: 1.2; color: #fff; }}
.cover .sub {{ font-size: 12pt; color: #b8c6dc; max-width: 150mm; }}
.cover .meta {{ position: absolute; left: 22mm; right: 22mm; bottom: 22mm; border-top: 1px solid #24324d;
  padding-top: 5mm; font-size: 8.5pt; color: #93a4bf; display: flex; justify-content: space-between; gap: 10mm; }}
.cover .meta .note {{ max-width: 120mm; }}
.cover .meta strong {{ color: #e8eef8; }}
.band {{ background: {NAVY}; color: #e8eef8; margin: 0 -17mm 8mm; padding: 10mm 17mm 8mm; position: relative; }}
.band .row {{ display: flex; align-items: center; gap: 6mm; }}
.band svg {{ width: 15mm; height: 18mm; }} .band .wm {{ font-size: 19pt; }}
.band h1 {{ margin: 0; font-size: 15pt; color: #fff; }} .band .sub {{ color: #b8c6dc; font-size: 9pt; margin-top: 1mm; }}
.band .tag {{ margin-left: auto; text-align: right; font-size: 8pt; color: #93a4bf; white-space: nowrap; }}
.band .tag b {{ display: block; color: {ACC}; font-size: 10.5pt; letter-spacing: 1.2px; }}
.band .wave {{ position: absolute; left: 0; bottom: -5mm; width: 100%; height: 6mm; }}
.band .note {{ font-size: 7.5pt; color: #93a4bf; margin-top: 3mm; }}
h2 {{ font-size: 13.5pt; color: {NAVY}; margin: 9mm 0 3mm; padding: 1.5mm 0 1.5mm 3.5mm; border-left: 1.6mm solid {ACC};
  break-after: avoid; }}
h2.zal {{ break-before: page; margin-top: 0; }}
h3 {{ font-size: 10.6pt; color: #0369a1; margin: 6mm 0 2mm; break-after: avoid; }}
p {{ margin: 0 0 2.5mm; }}
ul, ol {{ margin: 0 0 3mm; padding-left: 6mm; }} li {{ margin: .6mm 0; }}
ul li::marker {{ color: {ACC}; }} ol li::marker {{ color: #0369a1; font-weight: bold; }}
table {{ width: 100%; border-collapse: collapse; margin: 1mm 0 4mm; font-size: 8.4pt; break-inside: auto; }}
thead {{ display: table-header-group; }}
tr {{ break-inside: avoid; }}
th {{ background: {NAVY}; color: #fff; text-align: left; padding: 1.8mm 2.2mm; font-weight: bold; }}
th:first-child {{ border-top-left-radius: 1.5mm; }} th:last-child {{ border-top-right-radius: 1.5mm; }}
td {{ padding: 1.6mm 2.2mm; border-bottom: .25mm solid #e2e8f0; vertical-align: top; }}
tbody tr:nth-child(even) td {{ background: #f4f8fc; }}
code {{ font: 8pt 'DejaVu Sans Mono', monospace; background: #eef4fa; color: #0c4a6e; padding: .2mm 1mm; border-radius: 1mm; }}
th code {{ background: transparent; color: #fff; }}
figure {{ margin: 3mm 0 5mm; text-align: center; break-inside: avoid; }}
figure img {{ max-width: 100%; max-height: 120mm; border-radius: 2.5mm; }}
figcaption {{ font-size: 7.8pt; color: {MUTED}; margin-top: 1.5mm; }}
pre {{ font: 8.2pt/1.45 'DejaVu Sans Mono', monospace; background: #f4f8fc; border: .3mm solid #cbd5e1;
  border-left: 1.6mm solid {ACC}; border-radius: 2mm; padding: 4mm 5mm; white-space: pre-wrap; break-inside: avoid; color: #0f172a; }}
hr {{ border: 0; border-top: .3mm solid #e2e8f0; margin: 6mm 0; }}
.todo {{ background: #fef3c7; color: #92400e; border-radius: 1mm; padding: 0 1.2mm; font-weight: bold; font-size: .92em; }}
.karta h2 {{ margin-top: 5mm; font-size: 12pt; }}
.karta figure img {{ max-height: 62mm; }}
.karta table {{ break-inside: avoid; }}
.karta table {{ font-size: 8pt; }} .karta td {{ padding: 1.1mm 2.2mm; }} .karta th {{ padding: 1.4mm 2.2mm; }}
.karta p, .karta li {{ font-size: 9pt; }}
.karta .cols {{ display: grid; grid-template-columns: 1fr 1fr; gap: 7mm; }}
"""


def build(md_name):
    cfg = DOCS[md_name]
    src = (HERE / md_name).read_text().splitlines()
    cut = src.index("---")
    head, body = src[:cut], src[cut + 1:]
    title = next(l[2:] for l in head if l.startswith("# ")).replace("MonitorUp.pl – ", "")
    sub = next((l for l in head if l.startswith("**")), "")
    note = " ".join(l for l in head if l and not l.startswith(("#", "**")))
    body_html = md_blocks(body)
    if cfg["kind"] == "cover":
        top = (f"<section class='cover'><div class='brand'>{LOGO}{WORD}</div>{WAVE}"
               f"<h1>{inline(title[:1].upper() + title[1:])}</h1><div class='sub'>{inline(sub)}</div>"
               f"<div class='meta'><div class='note'>{inline(note)}</div><div><strong>www.monitorup.pl</strong></div></div></section>")
        main = f"<main>{body_html}</main>"
    else:
        top = (f"<header class='band'><div class='row'>{LOGO}<div><div class='wm'>MonitorUp<b>.pl</b></div>"
               f"<div class='sub'>{inline(sub)}</div></div><div class='tag'><b>{html.escape(title.upper())}</b>"
               f"HW 1.0 · SW 0.5.x</div></div><div class='note'>{inline(note)}</div>{WAVE}</header>")
        main = f"<main class='karta'>{body_html}</main>"
        top = ("<style>@page { margin-left: 0; margin-right: 0; @bottom-left { padding-left: 17mm } "
               "@bottom-right { padding-right: 17mm } @top-right { padding-right: 17mm } } @page :first { margin-top: 0 } "
               ".band { margin: 0 0 8mm } main.karta { padding: 0 17mm }</style>") + top
    doc = (f"<!doctype html><html lang='pl'><meta charset='utf-8'><title>MonitorUp.pl – {html.escape(title)}</title>"
           f"<style>:root{{--foot:\"{cfg['footer']}\"}}{CSS}</style><body>{top}{main}</body></html>")
    tmp = HERE / f"_{md_name}.html"
    tmp.write_text(doc)
    subprocess.run(["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={HERE / cfg['pdf']}", tmp.as_uri()], check=True, capture_output=True, timeout=120)
    tmp.unlink()
    print("OK", cfg["pdf"])


if __name__ == "__main__":
    for name in sys.argv[1:] or DOCS:
        build(name)
