// Monitor App - pliki strony WWW (wbudowane w program, dzialaja bez internetu)
#pragma once
#include <Arduino.h>

static const char LOGO_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#38bdf8"/><stop offset="1" stop-color="#0369a1"/></linearGradient><clipPath id="c"><rect x="12" y="8" width="40" height="48" rx="7"/></clipPath></defs><rect x="12" y="8" width="40" height="48" rx="7" fill="#0b1324" stroke="#38bdf8" stroke-width="3"/><g clip-path="url(#c)"><path d="M0 30 Q8 25 16 30 T32 30 T48 30 T64 30 V64 H0Z" fill="url(#g)"/><path d="M0 34 Q8 30 16 34 T32 34 T48 34 T64 34 V64 H0Z" fill="#0ea5e9" opacity=".55"/></g><path d="M20 20h8M20 26h5" stroke="#38bdf8" stroke-width="2.5" stroke-linecap="round"/></svg>)SVG";

static const char STYLE_CSS[] PROGMEM = R"CSS(
:root{--bg:#0b1220;--card:#131c2e;--card2:#1a2540;--line:#26324d;--txt:#e6edf7;--mut:#8b9bb8;--acc:#38bdf8;
--ok:#22c55e;--warn:#f59e0b;--crit:#ef4444;--high:#a855f7;--off:#64748b}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
a{color:var(--acc)}button,input,select{font:inherit}
.btn{background:var(--acc);color:#04121f;border:0;border-radius:10px;padding:10px 16px;font-weight:600;cursor:pointer}
.btn.sec{background:var(--card2);color:var(--txt);border:1px solid var(--line)}.btn:disabled{opacity:.5}
input,select{width:100%;background:#0d1626;border:1px solid var(--line);color:var(--txt);border-radius:10px;padding:10px 12px}
input:focus{outline:2px solid var(--acc);border-color:transparent}
label{display:block;color:var(--mut);font-size:13px;margin:12px 0 4px}
.err{color:var(--crit);min-height:20px;margin-top:10px;font-size:14px}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;letter-spacing:.5px}.brand img{width:34px;height:34px}
.brand b{color:var(--acc)}
/* logowanie */
.login{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;
background:radial-gradient(1000px 500px at 50% -10%,#123056 0%,transparent 60%),var(--bg)}
.lbox{width:100%;max-width:360px;background:var(--card);border:1px solid var(--line);border-radius:18px;padding:30px 26px;box-shadow:0 20px 60px #0008}
.lbox .logo{display:flex;flex-direction:column;align-items:center;margin-bottom:18px}
.lbox .logo img{width:84px;height:84px}.lbox h1{margin:8px 0 0;font-size:26px;letter-spacing:1px}
.lbox h1 b{color:var(--acc)}.lbox .sub{color:var(--mut);font-size:13px}
.lbox .btn{width:100%;margin-top:18px}.foot{text-align:center;color:var(--mut);font-size:12px;margin-top:16px}
/* aplikacja */
header{position:sticky;top:0;z-index:5;background:#0b1220e6;backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.hwrap{max-width:1200px;margin:auto;display:flex;align-items:center;gap:16px;padding:10px 16px}
nav{display:flex;gap:4px;margin-left:auto}nav a{color:var(--mut);text-decoration:none;padding:8px 12px;border-radius:9px}
nav a.on,nav a:hover{background:var(--card2);color:var(--txt)}
main{max-width:1200px;margin:auto;padding:18px 16px 90px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px}
.card h2{margin:0 0 12px;font-size:17px;display:flex;justify-content:space-between;align-items:center}
.tank{display:flex;gap:18px;align-items:center}.tank svg{flex:0 0 120px;height:190px}
.big{font-size:42px;font-weight:700;line-height:1}.big small{font-size:18px;color:var(--mut);font-weight:500}
.pct{font-size:20px;color:var(--mut);margin:6px 0 10px}
.badge{display:inline-block;padding:4px 10px;border-radius:999px;font-size:13px;font-weight:700;letter-spacing:.3px}
.s0{background:#22c55e22;color:var(--ok)}.s1{background:#f59e0b22;color:var(--warn)}.s2{background:#ef444433;color:var(--crit);animation:bl 1s infinite}
.s3{background:#a855f733;color:var(--high);animation:bl 1s infinite}.s4,.s5{background:#64748b33;color:#cbd5e1}
@keyframes bl{50%{opacity:.45}}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:13px;color:var(--mut);margin-top:12px}.kv b{color:var(--txt);font-weight:500}
.wave{animation:wv 3s linear infinite}@keyframes wv{to{transform:translateX(-40px)}}
.alarmbar{display:none;background:#3b0d12;border:1px solid #7f1d1d;color:#fecaca;border-radius:14px;padding:12px 16px;margin-bottom:16px;align-items:center;gap:12px}
.warnbar{display:none;background:#2f2307;border:1px solid #78530f;color:#fde68a;border-radius:14px;padding:10px 16px;margin-bottom:16px;align-items:center;gap:12px;justify-content:space-between}
.outs{display:grid;grid-template-columns:repeat(8,1fr);gap:6px}.o{background:var(--card2);border:1px solid var(--line);border-radius:10px;text-align:center;padding:8px 0;font-size:12px;color:var(--mut)}
.o.on{background:#38bdf822;border-color:var(--acc);color:var(--txt)}
.status{position:fixed;bottom:0;left:0;right:0;background:#0b1220f2;border-top:1px solid var(--line);font-size:12.5px;color:var(--mut)}
.status div{max-width:1200px;margin:auto;padding:8px 16px;display:flex;flex-wrap:wrap;gap:6px 18px}.status b{color:var(--txt);font-weight:500}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--off);margin-right:5px}.dot.g{background:var(--ok)}
.page{display:none}.page.on{display:block}
.muted{color:var(--mut)}.row{display:flex;gap:10px;flex-wrap:wrap;align-items:end}.row>*{flex:1}
.modal{position:fixed;inset:0;background:#000a;display:none;align-items:center;justify-content:center;z-index:20;padding:16px}
.modal .lbox{max-width:400px}
.subnav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:16px}.subnav a{padding:7px 12px;border-radius:9px;background:var(--card);border:1px solid var(--line);color:var(--mut);text-decoration:none;font-size:14px}
.subnav a.on{background:var(--acc);color:#04121f;border-color:var(--acc);font-weight:600}
.chk{display:flex;align-items:center;gap:8px;color:var(--txt);font-size:14px;margin:12px 0 4px}.chk input{width:auto}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}.g3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:0 12px}
table{width:100%;border-collapse:collapse;font-size:13.5px}th,td{text-align:left;padding:7px 6px;border-bottom:1px solid var(--line)}th{color:var(--mut);font-weight:500}
td input,td select{padding:6px 8px}
.toast{position:fixed;right:16px;bottom:56px;z-index:30;display:flex;flex-direction:column;gap:8px}
.toast div{background:var(--card2);border:1px solid var(--line);border-left:4px solid var(--ok);padding:10px 14px;border-radius:10px;box-shadow:0 8px 30px #0008;max-width:360px}
.toast div.e{border-left-color:var(--crit)}
.live{background:#0d1626;border:1px dashed var(--line);border-radius:10px;padding:8px 12px;font-size:13px;color:var(--mut);margin-top:10px}.live b{color:var(--txt)}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}.btn.red{background:var(--crit);color:#fff}.btn.sm{padding:6px 10px;font-size:13px}
canvas{width:100%;display:block}.chartbox{position:relative}.tip{position:absolute;pointer-events:none;background:#0b1220ee;border:1px solid var(--line);border-radius:8px;padding:6px 9px;font-size:12px;display:none;white-space:nowrap}
.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px}.stats div{background:var(--card2);border-radius:10px;padding:8px 10px;font-size:12px;color:var(--mut)}.stats b{display:block;color:var(--txt);font-size:16px}
.stats .warn b{color:var(--warn)}.wifil{max-height:220px;overflow:auto;margin-top:8px}.wifil div{padding:8px 10px;border-radius:8px;cursor:pointer;display:flex;justify-content:space-between}.wifil div:hover{background:var(--card2)}
.mono{font-family:ui-monospace,Consolas,monospace;word-break:break-all;background:#0d1626;padding:10px;border-radius:8px;border:1px solid var(--line)}
.legend{display:flex;gap:14px;font-size:13px;color:var(--mut);margin:6px 0}.legend i{display:inline-block;width:12px;height:3px;margin-right:6px;vertical-align:middle}
.ackb{margin-left:auto}
.g2>div,.g3>div{display:flex;flex-direction:column;justify-content:flex-end}
@media(max-width:600px){.g2,.g3{grid-template-columns:1fr}}
@media(max-width:600px){.hwrap{flex-wrap:wrap}nav{margin-left:0;width:100%}.big{font-size:34px}.tank svg{flex-basis:96px;height:160px}}
)CSS";

static const char LOGIN_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Monitor App - logowanie</title>
<link rel="icon" href="/logo.svg"><link rel="stylesheet" href="/style.css"></head><body>
<div class="login"><form class="lbox" id="f">
<div class="logo"><img src="/logo.svg" alt=""><h1>MONITOR <b>APP</b></h1><div class="sub">Monitoring poziomu zbiorników</div></div>
<label for="u">Login</label><input id="u" autocomplete="username" required autofocus>
<label for="p">Hasło</label><input id="p" type="password" autocomplete="current-password" required>
<button class="btn" id="b">Zaloguj</button><div class="err" id="e"></div>
<div class="foot"><a href="https://www.monitorapp.pl" target="_blank" rel="noopener">www.monitorapp.pl</a></div>
</form></div>
<script>
fetch('/api/v1/session').then(r=>{if(r.ok)location='/app'});
f.onsubmit=async ev=>{ev.preventDefault();b.disabled=true;e.textContent='';
try{const r=await fetch('/api/v1/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({user:u.value,pass:p.value})});
const j=await r.json();if(r.ok)location='/app';else e.textContent=j.error||'Błąd logowania';}catch(x){e.textContent='Brak połączenia z urządzeniem'}
b.disabled=false;};
</script></body></html>)HTML";

#include "web_app.h"
