// MonitorUp - aplikacja WWW po zalogowaniu (pulpit, logi, ustawienia) + mapa Modbus CSV
#pragma once
#include <Arduino.h>

static const char APP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>MonitorUp.pl</title>
<link rel="icon" href="/logo.svg"><link rel="stylesheet" href="/style.css"></head><body>
<header><div class="hwrap"><div class="brand"><img src="/logo.svg" alt="">MonitorUp<b>.pl</b></div>
<nav><a href="#pulpit" data-p="pulpit">Pulpit</a><a href="#logi" data-p="logi">Logi</a><a href="#ustawienia" data-p="ustawienia">Ustawienia</a>
<a href="#" id="lo">Wyloguj</a></nav></div></header>
<main>
<section class="page" id="pulpit">
 <div class="alarmbar" id="ab"><b>ALARM</b><span id="abt"></span><button class="btn red ackb" id="ackb">Kasuj alarm</button></div>
 <div class="warnbar" id="tw"><span>Czas urządzenia nie jest ustawiony. Logi i wykresy wymagają poprawnej daty.</span><button class="btn sec" id="tsync">Ustaw czas z przeglądarki</button></div>
 <div class="grid" id="tanks"></div>
 <div class="card" style="margin-top:16px"><h2>Wyjścia <span class="muted" style="font-size:13px;font-weight:400">OUT1–OUT8</span></h2><div class="outs" id="outs"></div>
 <h2 style="margin-top:16px">Wejścia <span class="muted" style="font-size:13px;font-weight:400">DI1–DI8 (DI1 = kasowanie alarmu)</span></h2><div class="outs" id="dis"></div></div>
</section>
<section class="page" id="logi">
 <div class="card"><h2>Wykres poziomu</h2>
  <div class="row"><div><label>Od</label><input type="date" id="lf"></div><div><label>Do</label><input type="date" id="lt"></div>
  <div style="flex:2" class="btns"><button class="btn sec sm" data-r="1">Doba</button><button class="btn sec sm" data-r="7">7 dni</button><button class="btn sec sm" data-r="30">30 dni</button>
  <button class="btn sm" id="lshow">Pokaż</button><button class="btn sec sm" id="lcsv">Pobierz CSV</button></div></div>
  <div class="legend" id="lleg"></div><div class="chartbox"><canvas id="lch" height="320"></canvas><div class="tip" id="ltip"></div></div>
  <p class="muted" style="font-size:12px;margin:6px 0 0">Zaznacz myszą fragment wykresu, aby go powiększyć. Dwuklik przywraca cały zakres.</p></div>
 <div class="card" style="margin-top:16px"><h2>Zużycie dzienne <select id="stk" style="width:auto"></select></h2><div class="chartbox"><canvas id="bch" height="220"></canvas><div class="tip" id="btip"></div></div></div>
 <div class="grid" style="margin-top:16px">
  <div class="card"><h2>Dziennik zdarzeń <select id="evm" style="width:auto"></select></h2><div style="max-height:420px;overflow:auto"><table id="evt"></table></div></div>
  <div class="card"><h2>Pliki na karcie SD</h2><div id="sdinfo" class="muted"></div><div style="max-height:380px;overflow:auto"><table id="sdf"></table></div></div>
 </div>
</section>
<section class="page" id="ustawienia"><div class="subnav" id="snav"></div><div id="sbody"></div></section>
</main>
<div class="status"><div id="sb">Łączenie…</div></div>
<div class="toast" id="toast"></div>
<div class="modal" id="mc"><div class="lbox"><div class="logo"><img src="/logo.svg" alt="" style="width:56px;height:56px"><h1 style="font-size:20px">Zmień hasło</h1>
<div class="sub">Ze względów bezpieczeństwa ustaw własne hasło zamiast domyślnego.</div></div>
<label>Nowe hasło (min. 6 znaków)</label><input type="password" id="m1" autocomplete="new-password"><label>Powtórz nowe hasło</label><input type="password" id="m2" autocomplete="new-password">
<button class="btn" id="mb">Zapisz hasło</button><div class="err" id="me"></div></div></div>
<div class="modal" id="md"><div class="lbox" style="max-width:520px"><h2 id="mdt" style="margin-top:0"></h2><div id="mdb"></div><div class="btns"><button class="btn" id="mdc">Zamknij</button></div></div></div>
<script src="/app.js"></script></body></html>)HTML";

static const char APP_JS[] PROGMEM = R"JS(
const $=id=>document.getElementById(id);
const ST=['NORMALNY','MINIMALNY','KRYTYCZNY','ZA DUŻY','BŁĄD CZUJNIKA','WYŁĄCZONY','NAGŁY SPADEK'];
const COL=['#22c55e','#f59e0b','#ef4444','#a855f7','#64748b','#64748b','#ef4444'];
const TC=['#38bdf8','#f59e0b'];
const fc=(v,d)=>v==null||isNaN(v)?'—':Number(v).toFixed(d).replace('.',',');
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const pad=n=>String(n).padStart(2,'0');
const dstr=d=>d.getFullYear()+'-'+pad(d.getMonth()+1)+'-'+pad(d.getDate());
const tstr=t=>t?new Date(t*1000).toLocaleString('pl-PL'):'—';
let S=null,C=null;
function toast(m,err){const d=document.createElement('div');if(err)d.className='e';d.textContent=m;$('toast').appendChild(d);setTimeout(()=>d.remove(),err?7000:3500)}
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location='/';throw 0}return r}
async function post(u,b){const r=await api(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})});
 let j={};try{j=await r.json()}catch(e){}if(!r.ok){toast(j.error||('Błąd '+r.status),1);throw 1}return j}
function modal(t,html){$('mdt').textContent=t;$('mdb').innerHTML=html;$('md').style.display='flex'}
$('mdc').onclick=()=>$('md').style.display='none';
function rebootWait(msg){modal('Restart urządzenia',`<p>${msg||'Urządzenie uruchamia się ponownie.'}</p><p class="muted" id="rbw">Czekam na urządzenie…</p>`);
 let n=0;const iv=setInterval(async()=>{n++;try{const r=await fetch('/api/v1/session');if(r.ok||r.status==401){clearInterval(iv);location.reload()}}catch(e){}
 if(n>40){clearInterval(iv);$('rbw').textContent='Urządzenie nie odpowiada pod tym adresem. Sprawdź nowy adres IP na wyświetlaczu OLED (ekran sieci).'}},3000)}

// ---------- nawigacja ----------
function show(p){const [m,s]=p.split('/');document.querySelectorAll('.page').forEach(x=>x.classList.toggle('on',x.id==m));
 document.querySelectorAll('nav a[data-p]').forEach(a=>a.classList.toggle('on',a.dataset.p==m));
 if(m=='logi')logsInit();if(m=='ustawienia')settingsShow(s||'siec')}
window.onhashchange=()=>show((location.hash||'#pulpit').slice(1));
$('lo').onclick=async e=>{e.preventDefault();await post('/api/v1/logout');location='/'};

// ---------- pulpit ----------
let hist=null;
function tankSvg(t,i){const h=150,top=20,lv=t.valid?t.pct/100:0,y=top+h-h*lv,c=COL[t.state];
 let m='';for(const [v,col] of [[t.thrCrit,'#ef4444'],[t.thrMin,'#f59e0b'],[t.thrHigh,'#a855f7']]){if(!t.height)break;
 const yy=top+h-h*Math.min(1,Math.max(0,v/t.height));m+=`<line x1="6" x2="22" y1="${yy}" y2="${yy}" stroke="${col}" stroke-width="2"/>`}
 return `<svg viewBox="0 0 120 190"><defs><clipPath id="tc${i}"><rect x="24" y="${top}" width="80" height="${h}" rx="12"/></clipPath>
 <linearGradient id="tg${i}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${c}" stop-opacity=".95"/><stop offset="1" stop-color="${c}" stop-opacity=".45"/></linearGradient></defs>
 <rect x="24" y="${top}" width="80" height="${h}" rx="12" fill="#0b1324"/>
 <g clip-path="url(#tc${i})"><g class="wave"><path d="M0 ${y} q10 -5 20 0 t20 0 t20 0 t20 0 t20 0 t20 0 t20 0 t20 0 V190 H0Z" fill="url(#tg${i})"/></g></g>
 <rect x="24" y="${top}" width="80" height="${h}" rx="12" fill="none" stroke="#2f3d5c" stroke-width="3"/>${m}
 <text x="64" y="${top+h+16}" fill="#8b9bb8" font-size="11" text-anchor="middle">${t.height?fc(t.height,0)+' cm':''}</text></svg>`}
function trendTxt(t){if(t.trend==null)return '';const a=t.trend>0.3?'↑':t.trend<-0.3?'↓':'→';return `${a} ${fc(t.trend,1)} cm/h`}
function statsHtml(t,i){if(!t.statsEn||!t.stats)return '';const s=t.stats,u=s.unit,d=u=='l'?0:1;
 let f=s.forecast==null?'brak danych':(s.forecast>=365?'> rok':'ok. '+fc(s.forecast,1)+' dni');
 if(s.forecast!=null&&s.forecast<365){const dd=new Date(Date.now()+s.forecast*864e5);f+=` (do ${pad(dd.getDate())}.${pad(dd.getMonth()+1)})`}
 return `<div class="stats"><div>Zużycie dziś<b>${fc(s.today,d)} ${u}</b></div><div>Średnio 7 dni<b>${fc(s.avg7,d)} ${u}/d</b></div>
 <div>Ostatnie tankowanie<b>${s.lastRefT?'+'+fc(s.lastRef,d)+' '+u:'—'}</b>${s.lastRefT?new Date(s.lastRefT*1000).toLocaleDateString('pl-PL'):''}${s.refilling?' (w toku)':''}</div></div>`}
function spark(i){if(!hist||!hist.tanks[i])return '';const a=hist.tanks[i],v=a.filter(x=>x!=null);if(v.length<2)return '';
 let mn=Math.min(...v),mx=Math.max(...v);if(mx-mn<1){mn-=0.5;mx+=0.5}const W=300,H=40;let p='',st=false;
 a.forEach((x,k)=>{if(x==null){st=false;return}const X=k/(a.length-1)*W,Y=H-2-(x-mn)/(mx-mn)*(H-4);p+=(st?'L':'M')+X.toFixed(1)+' '+Y.toFixed(1);st=true});
 return `<div class="muted" style="font-size:12px;margin-top:10px">Ostatnie 24 h (${fc(mn,1)}–${fc(mx,1)} cm)</div><svg viewBox="0 0 ${W} ${H}" style="width:100%;height:40px"><path d="${p}" fill="none" stroke="${TC[i]}" stroke-width="1.8"/></svg>`}
function render(s){S=s;let html='',al=[];
 s.tanks.forEach((t,i)=>{if(!t.en)return;if(t.state==2||t.state==3||t.state==6)al.push(t.name+': '+ST[t.state]+(t.state==6&&t.dropCm?' ('+fc(t.dropCm,1)+' cm)':''));
 const vol=t.volEn&&t.vol!=null?`<span>Objętość</span><b>${fc(t.vol,0)} / ${fc(t.volTotal,0)} l</b>`:'';
 html+=`<div class="card"><h2>${esc(t.name)}<span class="badge s${t.state==6?2:t.state}">${s.alarm.acked&&[2,3,6].includes(t.state)?'SKASOWANY – ':''}${ST[t.state]}</span></h2><div class="tank">${tankSvg(t,i)}<div style="flex:1">
 <div class="big">${t.valid?fc(t.cm,1):'—'} <small>cm</small></div><div class="pct">${t.valid?fc(t.pct,0)+' %':'brak pomiaru'} <span style="font-size:14px">${trendTxt(t)}</span></div>
 <div class="kv">${vol}<span>Prąd</span><b>${fc(t.mA,2)} mA</b><span>Wejście</span><b>A${t.ch+1}</b>
 <span>Progi</span><b>${fc(t.thrCrit,0)} / ${fc(t.thrMin,0)} / ${fc(t.thrHigh,0)} cm</b></div></div></div>${statsHtml(t,i)}${spark(i)}</div>`});
 $('tanks').innerHTML=html;
 $('ab').style.display=s.alarm.active?'flex':'none';$('abt').textContent=al.join(' · ')+(s.alarm.acked?' – skasowany':'');$('ackb').style.display=(s.alarm.acked||s.alarm.auto)?'none':'';
 $('tw').style.display=s.timeOk?'none':'flex';
 const FN=['wył.','ręczne','buzzer/syrena','lampka','alarm zb.','zbiornik 1','zbiornik 2','ostrzeż.','minimalny','maksymalny','krytyczny','usterka'];
 let o='',d='';for(let k=0;k<8;k++){o+=`<div class="o ${s.outLvl[k]?'on':''}">OUT${k+1}<br><small>${FN[s.outFunc[k]]}${s.outLvl[k]&&s.outLvl[k]<100?' '+s.outLvl[k]+'%':''}</small></div>`;d+=`<div class="o ${s.di>>k&1?'on':''}">DI${k+1}</div>`}
 $('outs').innerHTML=o;$('dis').innerHTML=d;
 const dt=s.timeOk?new Date(s.t*1000).toLocaleString('pl-PL'):'czas nieustawiony',up=s.up,n=s.net;
 $('sb').innerHTML=`<span><b>${dt}</b></span>
 <span><i class="dot ${n.eth.ip?'g':''}"></i>Ethernet <b>${n.eth.en?(n.eth.ip||(n.eth.link?'brak IP':'brak kabla')):'wyłączony'}</b></span>
 <span><i class="dot ${n.wifi.ip?'g':''}"></i>WiFi <b>${n.wifi.en?(n.wifi.ip?n.wifi.ip+' ('+n.wifi.rssi+' dBm)':'łączenie…'):'wyłączone'}</b></span>
 <span><i class="dot ${n.ap.on?'g':''}"></i>AP <b>${n.ap.on?esc(n.ap.ssid)+' ('+n.ap.cl+')':'wyłączony'}</b></span>
 <span><i class="dot ${s.sd.ok?'g':''}"></i>SD <b>${s.sd.ok?(s.sd.totalMB-s.sd.usedMB)+' MB wolne':'brak karty'}</b></span>
 <span><i class="dot ${s.mqtt?'g':''}"></i>MQTT</span><span>Modbus <b>${s.mb}</b></span>
 <span>Praca <b>${Math.floor(up/86400)?Math.floor(up/86400)+' d ':''}${Math.floor(up%86400/3600)} h ${Math.floor(up%3600/60)} min</b></span><span>v<b>${s.fw}</b></span>`;
 if(window.onStatus)window.onStatus(s)}
async function tick(){try{const r=await api('/api/v1/status');render(await r.json())}catch(e){if(e!==0)$('sb').textContent='Brak połączenia z urządzeniem…'}}
async function histLoad(){try{hist=await (await api('/api/v1/history')).json()}catch(e){}}
$('ackb').onclick=async()=>{await post('/api/v1/alarm/ack');toast('Alarm skasowany');tick()};
async function tsync(){await post('/api/v1/time',{epoch:Math.floor(Date.now()/1000)});toast('Ustawiono czas z przeglądarki');tick()}
$('tsync').onclick=tsync;

// ---------- wykresy ----------
function niceTicks(mn,mx,n){const sp=(mx-mn)||1,st0=Math.pow(10,Math.floor(Math.log10(sp/n))),e=sp/n/st0,st=(e>5?10:e>2?5:e>1?2:1)*st0,a=[];
 for(let v=Math.ceil(mn/st)*st;v<=mx+1e-9;v+=st)a.push(v);return a}
function lineChart(cv,tip,series,opt){const ctx=cv.getContext('2d'),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.height/ (cv._dpr||1);
 cv._dpr=dpr;cv.width=W*dpr;cv.height=H*dpr;cv.style.height=H+'px';ctx.scale(dpr,dpr);
 let t0=Infinity,t1=-Infinity,v0=Infinity,v1=-Infinity;series.forEach(s=>s.data.forEach(([t,v])=>{if(v==null)return;t0=Math.min(t0,t);t1=Math.max(t1,t);v0=Math.min(v0,v);v1=Math.max(v1,v)}));
 (opt.thr||[]).forEach(h=>{v0=Math.min(v0,h.v);v1=Math.max(v1,h.v)});
 if(opt.zoom){t0=opt.zoom[0];t1=opt.zoom[1]}
 if(!isFinite(t0)){ctx.fillStyle='#8b9bb8';ctx.font='14px system-ui';ctx.fillText('Brak danych w wybranym zakresie',20,40);return}
 if(v1-v0<1){v0-=1;v1+=1}const pv=(v1-v0)*0.08;v0-=pv;v1+=pv;if(t1==t0)t1=t0+60;
 const L=48,R=10,T=10,B=26,X=t=>L+(t-t0)/(t1-t0)*(W-L-R),Y=v=>T+(1-(v-v0)/(v1-v0))*(H-T-B);
 ctx.font='11px system-ui';ctx.fillStyle='#8b9bb8';ctx.strokeStyle='#26324d';ctx.lineWidth=1;
 niceTicks(v0,v1,5).forEach(v=>{ctx.beginPath();ctx.moveTo(L,Y(v));ctx.lineTo(W-R,Y(v));ctx.stroke();ctx.fillText(fc(v,v1-v0<10?1:0),4,Y(v)+4)});
 const span=t1-t0,tk=niceTicks(t0,t1,Math.max(2,Math.floor(W/110)));
 tk.forEach(t=>{const d=new Date(t*1000);const s=span>2*86400?pad(d.getDate())+'.'+pad(d.getMonth()+1):pad(d.getHours())+':'+pad(d.getMinutes());ctx.fillText(s,X(t)-14,H-8)});
 (opt.thr||[]).forEach(h=>{ctx.strokeStyle=h.c;ctx.setLineDash([5,4]);ctx.beginPath();ctx.moveTo(L,Y(h.v));ctx.lineTo(W-R,Y(h.v));ctx.stroke();ctx.setLineDash([])});
 ctx.save();ctx.beginPath();ctx.rect(L,T,W-L-R,H-T-B);ctx.clip();
 series.forEach(s=>{ctx.strokeStyle=s.c;ctx.lineWidth=1.8;ctx.beginPath();let pen=false,lt=0;
  s.data.forEach(([t,v])=>{if(v==null||(pen&&t-lt>opt.gap)){pen=false}if(v==null)return;pen?ctx.lineTo(X(t),Y(v)):ctx.moveTo(X(t),Y(v));pen=true;lt=t});ctx.stroke()});
 ctx.restore();
 let drag=null;
 cv.onmousemove=e=>{const r=cv.getBoundingClientRect(),mx=e.clientX-r.left;if(mx<L||mx>W-R){tip.style.display='none';return}
  const t=t0+(mx-L)/(W-L-R)*(t1-t0);let h=`<b>${new Date(t*1000).toLocaleString('pl-PL')}</b>`;
  series.forEach(s=>{let best=null;for(const p of s.data){if(best==null||Math.abs(p[0]-t)<Math.abs(best[0]-t))best=p}if(best&&best[1]!=null)h+=`<br><span style="color:${s.c}">■</span> ${esc(s.n)}: ${fc(best[1],1)} ${opt.unit}`});
  tip.innerHTML=h;tip.style.display='block';tip.style.left=Math.min(mx+12,W-170)+'px';tip.style.top='10px';
  if(drag!=null){ctx.fillStyle='#38bdf822';}};
 cv.onmouseleave=()=>tip.style.display='none';
 cv.onmousedown=e=>{const r=cv.getBoundingClientRect();drag=e.clientX-r.left};
 cv.onmouseup=e=>{const r=cv.getBoundingClientRect(),x2=e.clientX-r.left;if(drag!=null&&Math.abs(x2-drag)>10){const a=Math.min(drag,x2),b=Math.max(drag,x2);
  opt.zoom=[t0+(a-L)/(W-L-R)*(t1-t0),t0+(b-L)/(W-L-R)*(t1-t0)];lineChart(cv,tip,series,opt)}drag=null};
 cv.ondblclick=()=>{opt.zoom=null;lineChart(cv,tip,series,opt)}}
function barChart(cv,tip,labels,vals,full,color,unit){const ctx=cv.getContext('2d'),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.height/(cv._dpr||1);
 cv._dpr=dpr;cv.width=W*dpr;cv.height=H*dpr;cv.style.height=H+'px';ctx.scale(dpr,dpr);ctx.font='11px system-ui';
 if(!vals.length){ctx.fillStyle='#8b9bb8';ctx.font='14px system-ui';ctx.fillText('Brak danych – statystyki pojawią się po pierwszej pełnej dobie pracy',20,40);return}
 const mx=Math.max(1,...vals.map(v=>v||0))*1.15,L=48,B=26,T=10,bw=(W-L-10)/vals.length;
 niceTicks(0,mx,4).forEach(v=>{const y=T+(1-v/mx)*(H-T-B);ctx.strokeStyle='#26324d';ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(W-10,y);ctx.stroke();ctx.fillStyle='#8b9bb8';ctx.fillText(fc(v,0),4,y+4)});
 vals.forEach((v,k)=>{const h=(v||0)/mx*(H-T-B);ctx.fillStyle=full[k]?color:color+'66';ctx.fillRect(L+k*bw+2,H-B-h,Math.max(2,bw-4),h);
  if(vals.length<=31&&(k%Math.ceil(vals.length/10)==0)){ctx.fillStyle='#8b9bb8';ctx.fillText(labels[k],L+k*bw,H-8)}});
 cv.onmousemove=e=>{const r=cv.getBoundingClientRect(),k=Math.floor((e.clientX-r.left-L)/bw);if(k<0||k>=vals.length){tip.style.display='none';return}
  tip.innerHTML=`<b>${labels[k]}</b><br>${fc(vals[k],unit=='l'?0:1)} ${unit}${full[k]?'':' (niepełna doba)'}`;tip.style.display='block';tip.style.left=Math.min(e.clientX-r.left+10,W-150)+'px';tip.style.top='10px'};
 cv.onmouseleave=()=>tip.style.display='none'}

// ---------- logi ----------
let logsReady=false,lastRows=null,statsData=null;
function setRange(days){const t=new Date(),f=new Date(Date.now()-(days-1)*864e5);$('lf').value=dstr(f);$('lt').value=dstr(t)}
async function logsLoad(){if(!S)return;const f=$('lf').value,t=$('lt').value;
 try{const r=await api(`/api/v1/logs?from=${f}&to=${t}`);if(!r.ok){const j=await r.json();toast(j.error||'Błąd',1);return}
 const j=await r.json();lastRows=j.rows;drawLogs()}catch(e){}}
function drawLogs(){if(!lastRows||!S)return;const ser=[],thr=[],useL=false;
 S.tanks.forEach((t,i)=>{if(!t.en)return;ser.push({n:t.name,c:TC[i],data:lastRows.map(r=>[r[0],r[1+i]])})});
 const one=S.tanks.filter(t=>t.en);if(one.length==1){const t=one[0];thr.push({v:t.thrCrit,c:'#ef4444'},{v:t.thrMin,c:'#f59e0b'},{v:t.thrHigh,c:'#a855f7'})}
 $('lleg').innerHTML=ser.map(s=>`<span><i style="background:${s.c}"></i>${esc(s.n)}</span>`).join('')+(thr.length?'<span>progi: przerywane linie</span>':'');
 lineChart($('lch'),$('ltip'),ser,{thr,unit:'cm',gap:3600})}
async function statsLoad(){try{statsData=await (await api('/api/v1/stats')).json();const sel=$('stk');
 if(!sel.options.length)statsData.tanks.forEach((t,i)=>{if(t.en)sel.add(new Option(t.name,i))});sel.style.display=sel.options.length>1?'':'none';drawStats()}catch(e){}}
function drawStats(){if(!statsData)return;const t=statsData.tanks[$('stk').value||0];const L=[],V=[],F=[];
 t.days.forEach(d=>{const s=String(d[0]);L.push(s.slice(6,8)+'.'+s.slice(4,6));V.push(d[1]);F.push(d[3])});
 if(t.today!=null){L.push('dziś');V.push(t.today);F.push(0)}barChart($('bch'),$('btip'),L,V,F,'#38bdf8',t.unit)}
async function eventsLoad(){const m=$('evm').value;try{const j=await (await api('/api/v1/events'+(m?'?m='+m:''))).json();
 $('evt').innerHTML='<tr><th>Czas</th><th>Typ</th><th>Opis</th></tr>'+j.events.map(e=>`<tr><td style="white-space:nowrap">${tstr(e[0])}</td><td>${esc(e[1])}</td><td>${esc(e[2])}</td></tr>`).join('')}catch(e){}}
async function filesLoad(){try{const j=await (await api('/api/v1/logs/files')).json();
 $('sdinfo').textContent=j.sd?`Zajęte ${j.usedMB} MB z ${j.totalMB} MB`:'Brak karty SD';
 $('sdf').innerHTML='<tr><th>Dzień</th><th>Rozmiar</th><th></th></tr>'+(j.days||[]).sort().reverse().map(d=>`<tr><td>${d[0]}</td><td>${(d[1]/1024).toFixed(1)} kB</td><td><a href="/api/v1/logs/csv?from=${d[0]}&to=${d[0]}">CSV</a></td></tr>`).join('')}catch(e){}}
function logsInit(){if(!logsReady){logsReady=true;setRange(1);
 document.querySelectorAll('[data-r]').forEach(b=>b.onclick=()=>{setRange(+b.dataset.r);logsLoad()});
 $('lshow').onclick=logsLoad;$('lcsv').onclick=()=>location=`/api/v1/logs/csv?from=${$('lf').value}&to=${$('lt').value}`;
 $('stk').onchange=drawStats;const sel=$('evm');sel.add(new Option('bieżące (ostatnie 60)',''));
 for(let k=0;k<12;k++){const d=new Date();d.setDate(1);d.setMonth(d.getMonth()-k);const v=d.getFullYear()+'-'+pad(d.getMonth()+1);sel.add(new Option(v,v))}
 sel.onchange=eventsLoad;window.addEventListener('resize',()=>{drawLogs();drawStats()})}
 logsLoad();statsLoad();eventsLoad();filesLoad()}

// ---------- ustawienia: pola ----------
const F={txt:(id,l,v,ph='')=>`<div><label>${l}</label><input id="${id}" value="${esc(v)}" placeholder="${esc(ph)}"></div>`,
 num:(id,l,v,step='any')=>`<div><label>${l}</label><input id="${id}" type="number" step="${step}" value="${v??''}"></div>`,
 pw:(id,l,ph='')=>`<div><label>${l}</label><input id="${id}" type="password" autocomplete="new-password" placeholder="${esc(ph)}"></div>`,
 chk:(id,l,v)=>`<label class="chk"><input type="checkbox" id="${id}" ${v?'checked':''}>${l}</label>`,
 sel:(id,l,v,o)=>`<div><label>${l}</label><select id="${id}">${o.map(([k,t])=>`<option value="${k}" ${k==v?'selected':''}>${t}</option>`).join('')}</select></div>`};
const V=id=>$(id).value.trim(),N=id=>parseFloat(String($(id).value).replace(',','.')),B=id=>$(id).checked;
function netFields(p,n){return F.sel(p+'dhcp','Adres IP',n.dhcp?1:0,[[1,'automatycznie (DHCP)'],[0,'statyczny']])+
 `<div id="${p}st" class="g2" style="display:${n.dhcp?'none':'grid'}">`+F.txt(p+'ip','Adres IP',n.ip,'192.168.1.50')+F.txt(p+'mask','Maska',n.mask||'255.255.255.0')+
 F.txt(p+'gw','Brama',n.gw)+F.txt(p+'dns1','DNS 1',n.dns1)+F.txt(p+'dns2','DNS 2',n.dns2)+'</div>'}
function netBind(p){$(p+'dhcp').onchange=()=>$(p+'st').style.display=$(p+'dhcp').value=='1'?'none':'grid'}
function netVal(p){return {dhcp:$(p+'dhcp').value=='1',ip:V(p+'ip'),mask:V(p+'mask'),gw:V(p+'gw'),dns1:V(p+'dns1'),dns2:V(p+'dns2')}}
async function save(obj,msg){const j=await post('/api/v1/config',obj);if(j.reboot){rebootWait('Zapisano ustawienia sieci. Urządzenie uruchamia się ponownie z nowymi ustawieniami.')}else{toast(msg||'Zapisano');await cfgLoad()}}
async function cfgLoad(){C=await (await api('/api/v1/config')).json()}
const TABS=[['siec','Sieć'],['przetworniki','Przetworniki'],['alarmy','Alarmy'],['wyjscia','Wyjścia'],['statystyki','Statystyki'],['czas','Data i czas'],['logi','Logi'],['mqtt','MQTT'],['modbus','Modbus'],['api','API'],['konto','Konto'],['system','System']];
async function settingsShow(tab){window.onStatus=null;if(!C){$('sbody').innerHTML='<div class="card muted">Wczytywanie ustawień…</div>';try{await cfgLoad()}catch(e){$('sbody').innerHTML='<div class="card">Nie udało się wczytać ustawień. <a href="javascript:location.reload()">Odśwież stronę</a></div>';return}}
 $('snav').innerHTML=TABS.map(([k,t])=>`<a href="#ustawienia/${k}" class="${k==tab?'on':''}">${t}</a>`).join('');
 const fn=SET[tab]||SET.siec;$('sbody').innerHTML='';fn($('sbody'))}
const SET={};

SET.siec=el=>{const n=S?S.net:{eth:{},wifi:{},ap:{}};
 el.innerHTML=`<div class="grid"><div class="card"><h2>Ethernet</h2><div class="live" id="ethl"></div>${F.chk('een','Ethernet włączony',C.eth.en)}${netFields('e_',C.eth.net)}
 <div class="btns"><button class="btn" id="esv">Zapisz</button></div></div>
 <div class="card"><h2>WiFi (klient)</h2><div class="live" id="wfl"></div>${F.chk('wen','Łącz z siecią WiFi',C.wifi.en)}
 <div class="btns"><button class="btn sec sm" id="wsc">Szukaj sieci</button></div><div class="wifil" id="wlist"></div>
 ${F.txt('wss','Nazwa sieci (SSID)',C.wifi.ssid)}${F.pw('wpw','Hasło sieci',C.wifi.hasPass?'•••••• (bez zmian)':'')}${netFields('w_',C.wifi.net)}
 <div class="btns"><button class="btn" id="wsv">Połącz i zapisz</button></div></div>
 <div class="card"><h2>Access Point</h2>${F.sel('apm','Tryb',C.ap.mode,[[0,'zawsze włączony'],[1,'tylko awaryjnie (brak WiFi i Ethernetu przez 60 s)'],[2,'wyłączony']])}
 ${F.txt('aps','Nazwa sieci (SSID)',C.ap.ssid)}${F.pw('app','Hasło (min. 8 znaków)','•••••• (bez zmian)')}${F.num('apc','Kanał (1–13)',C.ap.channel,1)}
 <p class="muted" style="font-size:13px">Adres urządzenia w sieci AP: 192.168.4.1. Po połączeniu z WiFi AP pracuje na kanale routera.</p>
 <div class="btns"><button class="btn" id="asv">Zapisz</button></div></div></div>
 <p class="muted" style="font-size:13px">Zapis ustawień sieci uruchamia urządzenie ponownie. Jeśli adres się zmieni, nowy adres IP jest widoczny na wyświetlaczu (ekran sieci) i przez sieć AP.</p>`;
 netBind('e_');netBind('w_');
 window.onStatus=s=>{const e=s.net.eth,w=s.net.wifi;$('ethl')&&($('ethl').innerHTML=`Łącze: <b>${e.link?e.speed+' Mb/s':'brak kabla'}</b><br>IP: <b>${e.ip||'—'}</b>${e.gw?'<br>Brama: <b>'+e.gw+'</b>':''}<br>MAC: <b>${e.mac||'—'}</b>`);
  $('wfl')&&($('wfl').innerHTML=w.en?`Stan: <b>${w.ip?'połączono z '+esc(w.ssid):'łączenie z '+esc(w.ssid)+'…'}</b>${w.ip?'<br>IP: <b>'+w.ip+'</b><br>Sygnał <b>'+w.rssi+' dBm</b>':''}`:'WiFi wyłączone')};
 if(S)window.onStatus(S);
 $('esv').onclick=()=>{if(confirm('Zapisać ustawienia Ethernetu i uruchomić urządzenie ponownie?'))save({eth:{en:B('een'),net:netVal('e_')}})};
 $('wsv').onclick=()=>{const w={en:B('wen'),ssid:V('wss'),net:netVal('w_')};if(V('wpw'))w.pass=$('wpw').value;if(confirm('Zapisać ustawienia WiFi i uruchomić urządzenie ponownie?'))save({wifi:w})};
 $('asv').onclick=()=>{const a={mode:+V('apm'),ssid:V('aps'),channel:+V('apc')};if($('app').value)a.pass=$('app').value;if(confirm('Zapisać ustawienia Access Pointa i uruchomić urządzenie ponownie?'))save({ap:a})};
 $('wsc').onclick=async()=>{$('wlist').innerHTML='<div class="muted">Szukam sieci…</div>';await api('/api/v1/wifi/scan?start=1');
  for(let k=0;k<20;k++){await new Promise(r=>setTimeout(r,1000));const j=await (await api('/api/v1/wifi/scan')).json();if(!j.running){
   const l=(j.list||[]).sort((a,b)=>b.rssi-a.rssi);$('wlist').innerHTML=l.length?l.map(x=>`<div data-s="${esc(x.ssid)}"><span>${x.open?'':'🔒 '}${esc(x.ssid)}</span><span class="muted">${x.rssi} dBm · kanał ${x.ch}</span></div>`).join(''):'<div class="muted">Nie znaleziono sieci</div>';
   $('wlist').querySelectorAll('[data-s]').forEach(d=>d.onclick=()=>{$('wss').value=d.dataset.s;$('wen').checked=true;$('wpw').focus()});return}}
  $('wlist').innerHTML='<div class="muted">Skanowanie trwa zbyt długo</div>'}};

const SHAPES=[[0,'walec leżący'],[1,'walec stojący'],[2,'prostopadłościan'],[3,'tabela cm → litry']];
function volCalc(t,cm){const a=t.dimA,b=t.dimB;cm=Math.max(0,cm);if(t.shape==0){const r=a/2,h=Math.min(cm,a);return (r*r*Math.acos((r-h)/r)-(r-h)*Math.sqrt(Math.max(0,2*r*h-h*h)))*b/1000}
 if(t.shape==1)return Math.PI*(a/2)**2*cm/1000;if(t.shape==2)return a*b*cm/1000;const tb=t.table||[];if(tb.length<2)return 0;if(cm<=tb[0][0])return tb[0][1];
 for(let k=1;k<tb.length;k++)if(cm<=tb[k][0])return tb[k-1][1]+(cm-tb[k-1][0])*(tb[k][1]-tb[k-1][1])/(tb[k][0]-tb[k-1][0]);return tb[tb.length-1][1]}
SET.przetworniki=el=>{const vis=C.tanks.map((t,i)=>i==0||t.en);el.innerHTML='<div class="grid">'+C.tanks.map((t,i)=>!vis[i]?'':`<div class="card"><h2>${i==0?'Przetwornik zbiornika':'Drugi zbiornik'}</h2>
 ${F.txt('tn'+i,'Nazwa',t.name)}${F.chk('ten'+i,'Pomiar włączony',t.en)}
 <div class="g2">${F.sel('tch'+i,'Wejście',t.ch,[[0,'A1 (0–5 V)'],[1,'A2 (0–5 V)'],[2,'A3 (4–20 mA)'],[3,'A4 (4–20 mA)']])}${F.sel('tty'+i,'Typ sygnału',t.type,[[0,'4–20 mA'],[1,'0–5 V']])}</div>
 <div class="g2">${F.num('tsh'+i,'Rezystor pomiarowy [Ω] (przelicza V → mA)',t.shunt)}${F.num('tsm'+i,'Dodatkowe wygładzanie [%]',t.smooth,1)}</div>
 <div class="live" id="tl${i}"></div>
 <h2 style="margin-top:14px;font-size:15px">Kalibracja</h2>
 <div class="g2">${F.num('tminx'+i,'Punkt MIN – odczyt [mA lub V]',t.minX)}${F.num('tminc'+i,'Punkt MIN – poziom [cm]',t.minCm)}
 ${F.num('tmaxx'+i,'Punkt MAX – odczyt [mA lub V]',t.maxX)}${F.num('tmaxc'+i,'Punkt MAX – poziom [cm]',t.maxCm)}</div>
 <div class="btns"><button class="btn sec sm" onclick="calib(${i},'min')">Zapisz bieżący odczyt jako MIN</button><button class="btn sec sm" onclick="calib(${i},'max')">Zapisz bieżący odczyt jako MAX</button></div>
 <p class="muted" style="font-size:12px">Przy pustym (lub zmierzonym miarką) zbiorniku wpisz poziom w polu „poziom [cm]” punktu i kliknij przycisk. Drugi punkt wykonaj przy innym, znanym poziomie.</p>
 <div class="g2">${F.num('tof'+i,'Przesunięcie montażowe sondy [cm]',t.offset)}${F.num('th'+i,'Wysokość zbiornika (100 %) [cm]',t.height)}</div>
 <h2 style="margin-top:14px;font-size:15px">Litry</h2>${F.chk('tve'+i,'Przeliczaj na litry',t.volEn)}
 <div id="tvb${i}">${F.sel('tsp'+i,'Kształt zbiornika',t.shape,SHAPES)}<div class="g2" id="tdim${i}"></div>
 <div id="ttb${i}"><label>Tabela: w każdej linii „cm;litry” (min. 2 punkty)</label><textarea id="ttt${i}" rows="5" style="width:100%;background:#0d1626;color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:8px">${(t.table||[]).map(p=>p[0]+';'+p[1]).join('\n')}</textarea></div>
 <div class="live" id="tcap${i}"></div></div>
 <div class="btns"><button class="btn" onclick="saveTank(${i})">Zapisz</button></div></div>`).join('')+(vis[1]?'':`<div class="card"><h2>Drugi zbiornik</h2><p class="muted">Urządzenie pracuje z jednym zbiornikiem. Drugi przetwornik (np. na A4) można dodać w razie potrzeby.</p><div class="btns"><button class="btn sec" onclick="addTank2()">Dodaj drugi zbiornik</button></div></div>`)+'</div>';
 C.tanks.forEach((t,i)=>{if(!vis[i])return;const dims=()=>{const s=+V('tsp'+i);const d=$('tdim'+i);const cur={a:$('tda'+i)?N('tda'+i):t.dimA,b:$('tdb'+i)?N('tdb'+i):t.dimB};
  d.innerHTML=s==0?F.num('tda'+i,'Średnica wewnętrzna [cm]',cur.a)+F.num('tdb'+i,'Długość [cm]',cur.b):s==1?F.num('tda'+i,'Średnica wewnętrzna [cm]',cur.a):s==2?F.num('tda'+i,'Długość [cm]',cur.a)+F.num('tdb'+i,'Szerokość [cm]',cur.b):'';
  $('ttb'+i).style.display=s==3?'':'none';$('tvb'+i).style.display=B('tve'+i)?'':'none';d.querySelectorAll('input').forEach(x=>x.oninput=cap);cap()};
  const cap=()=>{const o=tankForm(i);$('tcap'+i).innerHTML=`Pojemność całkowita: <b>${fc(volCalc(o,o.shape==0?o.dimA:o.height),0)} l</b>`};
  $('tsp'+i).onchange=dims;$('tve'+i).onchange=dims;$('ttt'+i).oninput=cap;$('th'+i).oninput=cap;dims()});
 window.onStatus=s=>s.tanks.forEach((t,i)=>{const e=$('tl'+i);if(e)e.innerHTML=`Na żywo: surowy <b>${t.raw}</b> · napięcie <b>${fc(t.v,3)} V</b> · prąd <b>${fc(t.mA,2)} mA</b> · poziom <b>${t.valid?fc(t.cm,1)+' cm':ST[t.state]}</b>`});
 if(S)window.onStatus(S)};
function tankForm(i){const s=+V('tsp'+i);const tb=V('ttt'+i).split('\n').map(l=>l.split(/[;\t]/).map(x=>parseFloat(x.replace(',','.')))).filter(p=>p.length>=2&&!isNaN(p[0])&&!isNaN(p[1])).sort((a,b)=>a[0]-b[0]);
 return {name:V('tn'+i),en:B('ten'+i),ch:+V('tch'+i),type:+V('tty'+i),shunt:N('tsh'+i),smooth:+V('tsm'+i),minX:N('tminx'+i),minCm:N('tminc'+i),maxX:N('tmaxx'+i),maxCm:N('tmaxc'+i),
 offset:N('tof'+i),height:N('th'+i),volEn:B('tve'+i),shape:s,dimA:$('tda'+i)?N('tda'+i):C.tanks[i].dimA,dimB:$('tdb'+i)?N('tdb'+i):C.tanks[i].dimB,table:tb}}
window.addTank2=async()=>{if(!confirm('Włączyć drugi zbiornik?'))return;await save({tanks:[{},{en:true}]},'Dodano drugi zbiornik');settingsShow('przetworniki')};
window.saveTank=i=>{const t=C.tanks.map(()=>({}));t[i]=tankForm(i);save({tanks:t},'Zapisano ustawienia przetwornika')};
window.calib=async(i,p)=>{const cm=N(p=='min'?'tminc'+i:'tmaxc'+i);if(isNaN(cm)){toast('Wpisz poziom [cm] dla punktu',1);return}
 if(!confirm(`Zapisać bieżący odczyt jako punkt ${p.toUpperCase()} = ${cm} cm?`))return;const j=await post('/api/v1/calibrate',{tank:i,point:p,cm});
 toast(`Zapisano punkt ${p.toUpperCase()}: ${fc(j.x,3)} → ${cm} cm`);await cfgLoad();settingsShow('przetworniki')};

SET.alarmy=el=>{el.innerHTML='<div class="grid">'+C.tanks.map((t,i)=>!t.en?'':`<div class="card"><h2>${esc(t.name)}</h2>
 <div class="g3">${F.num('ac'+i,'KRYTYCZNY ≤ [cm]',t.thrCrit)}${F.num('am'+i,'MINIMALNY ≤ [cm]',t.thrMin)}${F.num('ah'+i,'ZA DUŻY ≥ [cm]',t.thrHigh)}</div>
 <div class="g2">${F.num('ahy'+i,'Histereza [cm]',t.hyst)}${F.num('ad'+i,'Opóźnienie alarmu [s]',t.delay,1)}</div>
 <p class="muted" style="font-size:12px">Kolejność: krytyczny &lt; minimalny &lt; za duży. Wysokość zbiornika: ${fc(t.height,0)} cm.</p></div>`).join('')+
 `<div class="card"><h2>Kasowanie i przypomnienia</h2>${F.sel('aam','Kasowanie alarmu',C.alarm.ackAuto?1:0,[[0,'ręczne – alarm trwa do skasowania (przycisk DI1, strona, Modbus, MQTT, API)'],[1,'automatyczne – bez kasowania, alarm gaśnie po powrocie poziomu do normy']])}${F.chk('adi','Kasowanie alarmu wejściem DI1 (przycisk)',C.alarm.ackDi1)}
 ${F.num('arm','Przypomnienie: ponowna syrena po [min] (0 = wyłączone)',C.alarm.reminderMin,1)}
 ${F.sel('alm','Lampka przy stanie MINIMALNYM / błędzie czujnika',C.alarm.lampOnMin,[[0,'wyłączona'],[1,'słabe mruganie']])}</div></div>
 <div class="btns"><button class="btn" id="asv2">Zapisz</button></div>`;
 $('asv2').onclick=()=>save({tanks:C.tanks.map((t,i)=>!t.en?{}:({thrCrit:N('ac'+i),thrMin:N('am'+i),thrHigh:N('ah'+i),hyst:N('ahy'+i),delay:+V('ad'+i)})),
  alarm:{ackAuto:+V('aam')==1,ackDi1:B('adi'),reminderMin:+V('arm'),lampOnMin:+V('alm')}})};

const FUNCS=[[0,'wyłączone'],[1,'ręczne'],[2,'buzzer / syrena (milknie po skasowaniu)'],[3,'lampka'],[4,'alarm zbiorczy'],[5,'stan zbiornika 1'],[6,'stan zbiornika 2'],[7,'ostrzeżenie (MINIMALNY)'],[8,'poziom MINIMALNY'],[9,'poziom MAKSYMALNY (za duży)'],[10,'poziom KRYTYCZNY'],[11,'usterka zbiorcza (wg ustawień niżej)']];
const FAULTS=[[1,'błąd czujnika (przetwornik odłączony / prąd poza 3,6–21 mA)'],[2,'przetwornik A/C ADS1115 nie odpowiada'],[4,'brak lub błąd karty SD'],[8,'czas nieustawiony (RTC i NTP)'],[16,'brak sieci (Ethernet i WiFi)'],[32,'MQTT włączone, ale brak połączenia z brokerem'],[64,'ekspander wyjść PCF8575 nie odpowiada']];
SET.wyjscia=el=>{const a=C.alarm;el.innerHTML=`<div class="card"><h2>Funkcje wyjść</h2><div style="overflow:auto"><table><tr><th>Wyjście</th><th>Funkcja</th><th>Jasność przed skasowaniem [%]</th><th>Po skasowaniu [%]</th><th>Przerywane</th><th>Wł./wył. [ms]</th><th>Ręczne [%]</th><th>Teraz</th></tr>`+
 a.out.map((o,k)=>`<tr><td>OUT${k+1}</td><td><select id="of${k}">${FUNCS.map(([v,t])=>`<option value="${v}" ${v==o.func?'selected':''}>${t}</option>`).join('')}</select></td>
 <td><input id="op${k}" type="number" min="0" max="100" step="10" value="${o.pre}"></td><td><input id="oq${k}" type="number" min="0" max="100" step="10" value="${o.post}"></td>
 <td><input type="checkbox" id="ob${k}" ${o.blink?'checked':''} style="width:auto"></td><td style="white-space:nowrap"><input id="on${k}" type="number" value="${o.onMs}" style="width:70px"> / <input id="oo${k}" type="number" value="${o.offMs}" style="width:70px"></td>
 <td><input id="om${k}" type="range" min="0" max="100" step="10" value="${o.manual}" onchange="manOut(${k})"></td><td id="ol${k}">—</td></tr>`).join('')+`</table></div>
 <p class="muted" style="font-size:12px">PWM programowy ok. 100 Hz w krokach 10 %. Lampka z buzzerem: ustaw „po skasowaniu” np. 40 %, żeby lampka świeciła słabiej po skasowaniu alarmu. Działanie buzzera przy obniżonej jasności zależy od lampki – sprawdź na swoim egzemplarzu. Syrena: „po skasowaniu” 0 %. Alarm zbiorczy: 100 % (trzyma do powrotu do normy).</p>
 ${F.sel('olg','Lampka: dodatkowo sprzętowy PWM na wolnym GPIO (przez zewnętrzny moduł MOSFET)',a.lampGpio,[[0,'nie'],[13,'GPIO13'],[14,'GPIO14'],[21,'GPIO21']])}
 <div class="btns"><button class="btn" id="osv">Zapisz</button><button class="btn sec" id="otst">Test wyjść teraz</button></div></div>
 <div class="card" style="margin-top:16px"><h2>Usterka zbiorcza – co sygnalizuje</h2><p class="muted" style="font-size:13px">Wyjście z funkcją „usterka zbiorcza” włącza się, gdy wystąpi którakolwiek z zaznaczonych przyczyn. Kolor kropki: stan teraz.</p>
 ${FAULTS.map(([b,t])=>`<label class="chk" style="display:flex;align-items:center;gap:8px;margin:6px 0;cursor:pointer"><input type="checkbox" id="fm${b}" ${(a.faultMask??7)&b?'checked':''} style="width:auto"><i class="dot" id="fd${b}"></i>${t}</label>`).join('')}
 <div class="btns"><button class="btn" id="fsv">Zapisz</button></div></div>
 <div class="card" style="margin-top:16px"><h2>Test wyjść przy starcie</h2>${F.chk('bte','Test przy włączeniu zasilania',a.bootTest)}
 <label>Wyjścia w teście</label><div class="outs">${[0,1,2,3,4,5,6,7].map(k=>`<label class="o" style="cursor:pointer"><input type="checkbox" id="bm${k}" ${a.bootMask>>k&1?'checked':''} style="width:auto"> OUT${k+1}</label>`).join('')}</div>
 <div class="g2">${F.num('bms','Czas na wyjście [ms]',a.bootMs,100)}<div>${F.chk('bss','Pomijaj po restarcie programowym (watchdog, aktualizacja)',a.bootSkipSw)}</div></div>
 <div class="btns"><button class="btn" id="bsv">Zapisz</button></div></div>`;
 window.onStatus=s=>{for(let k=0;k<8;k++){const e=$('ol'+k);if(e)e.textContent=s.outLvl[k]?s.outLvl[k]+' %':'wył.'}FAULTS.forEach(([b])=>{const d=$('fd'+b);if(d)d.className='dot '+((s.fault&b)?'r':'g')})};
 $('fsv').onclick=()=>save({alarm:{faultMask:FAULTS.reduce((m,[b])=>m|(B('fm'+b)?b:0),0)}});if(S)window.onStatus(S);
 const outs=()=>a.out.map((o,k)=>({func:+V('of'+k),pre:+V('op'+k),post:+V('oq'+k),blink:B('ob'+k),onMs:+V('on'+k),offMs:+V('oo'+k),manual:+V('om'+k)}));
 $('osv').onclick=()=>save({alarm:{out:outs(),lampGpio:+V('olg')}},'Zapisano ustawienia wyjść');
 $('otst').onclick=async()=>{await post('/api/v1/outputs',{test:true});toast('Test wyjść uruchomiony')};
 $('bsv').onclick=()=>{let m=0;for(let k=0;k<8;k++)if(B('bm'+k))m|=1<<k;save({alarm:{bootTest:B('bte'),bootMask:m,bootMs:+V('bms'),bootSkipSw:B('bss')}})}};
window.manOut=async k=>{if(+V('of'+k)!=1||C.alarm.out[k].func!=1){toast('Najpierw ustaw i zapisz funkcję „ręczne”',1);return}await post('/api/v1/outputs',{n:k+1,value:+V('om'+k)})};

SET.statystyki=el=>{el.innerHTML='<div class="grid">'+C.tanks.map((t,i)=>!t.en?'':`<div class="card"><h2>${esc(t.name)}</h2>
 ${F.num('sr'+i,'Tankowanie: wzrost o ≥ [cm] w 15 min',t.refill)}
 <h2 style="margin-top:14px;font-size:15px">Alarm nagłego spadku (wyciek / kradzież)</h2>${F.chk('sd'+i,'Włączony',t.dropEn)}
 <div class="g2">${F.num('sdc'+i,'Spadek o ≥ [cm]',t.dropCm)}${F.num('sdm'+i,'w czasie [min]',t.dropMin,1)}</div>
 ${F.chk('sn'+i,'Okno nocne (czulszy próg)',t.nightEn)}<div class="g3">${F.num('snf'+i,'Od godziny',t.nightFrom,1)}${F.num('snt'+i,'Do godziny',t.nightTo,1)}${F.num('snc'+i,'Spadek o ≥ [cm]',t.nightCm)}</div></div>`).join('')+'</div><div class="btns"><button class="btn" id="ssv">Zapisz</button></div>';
 $('ssv').onclick=()=>save({tanks:C.tanks.map((t,i)=>!t.en?{}:({refill:N('sr'+i),dropEn:B('sd'+i),dropCm:N('sdc'+i),dropMin:+V('sdm'+i),
  nightEn:B('sn'+i),nightFrom:+V('snf'+i),nightTo:+V('snt'+i),nightCm:N('snc'+i)}))})};

SET.czas=el=>{const t=C.time;el.innerHTML=`<div class="grid"><div class="card"><h2>Data i czas</h2><div class="live" id="tml"></div>
 <div class="btns"><button class="btn sec" id="tbr">Ustaw czas z przeglądarki</button><button class="btn sec" id="tns">Synchronizuj NTP teraz</button></div>
 <label>Ręcznie</label><div class="row"><input type="datetime-local" id="tman"><button class="btn sec" id="tms" style="flex:0">Ustaw</button></div></div>
 <div class="card"><h2>Synchronizacja NTP</h2>${F.chk('tne','Synchronizuj z serwerem NTP',t.ntp)}${F.txt('ts1','Serwer NTP',t.server)}${F.txt('ts2','Serwer zapasowy',t.server2)}
 ${F.num('tin','Synchronizuj co [min]',t.intervalMin,1)}<p class="muted" style="font-size:12px">Strefa czasowa: Polska (CET/CEST, automatyczny czas letni). Zegar DS3231 z baterią podtrzymuje czas bez sieci.</p>
 <div class="btns"><button class="btn" id="tsv">Zapisz</button></div></div></div>`;
 window.onStatus=s=>{const e=$('tml');if(e)e.innerHTML=`Czas urządzenia: <b>${s.timeOk?new Date(s.t*1000).toLocaleString('pl-PL'):'nieustawiony'}</b><br>Źródło: <b>${esc(s.timeSrc)}</b><br>Przeglądarka: <b>${new Date().toLocaleString('pl-PL')}</b>`};if(S)window.onStatus(S);
 $('tbr').onclick=tsync;$('tns').onclick=async()=>{await post('/api/v1/time/sync');toast('Wysłano żądanie synchronizacji NTP')};
 $('tms').onclick=async()=>{const v=$('tman').value;if(!v)return;await post('/api/v1/time',{epoch:Math.floor(new Date(v).getTime()/1000),src:'ręcznie'});toast('Ustawiono czas')};
 $('tsv').onclick=()=>save({time:{ntp:B('tne'),server:V('ts1'),server2:V('ts2'),intervalMin:+V('tin')}})};

SET.logi=el=>{el.innerHTML=`<div class="card" style="max-width:560px"><h2>Logi na karcie SD</h2>${F.num('lgi','Zapis pomiarów co [min] (1–60)',C.logs.intervalMin,1)}
 ${F.num('lgr','Przechowuj pliki przez [dni]',C.logs.retentionDays,1)}<p class="muted" style="font-size:12px">Pliki dzienne /logs/RRRR/MM/RRRR-MM-DD.csv (separator ;, przecinek dziesiętny – otwierają się w Excelu). Dodatkowy wpis przy każdej zmianie stanu alarmu. Przy &lt; 10 % wolnego miejsca najstarsze pliki są usuwane.</p>
 <div class="btns"><button class="btn" id="lgs">Zapisz</button><button class="btn red" id="lgc">Usuń wszystkie logi</button></div></div>
 <div class="card" style="max-width:560px;margin-top:16px"><h2>Test karty SD</h2><p class="muted" style="font-size:13px">Sprawdza montowanie (20/10/4/1 MHz), typ i pojemność karty oraz zapis i odczyt pliku testowego.</p><div class="live" style="line-height:1.6"><b>Jak urządzenie pilnuje karty</b><br>Zamontowana karta jest sprawdzana co 10 s, a także przy każdym zapisie.<br>Bez karty urządzenie próbuje ją zamontować co 30 s.<br><b>Wyjęcie w trakcie pracy:</b> wykrycie w ciągu ok. 10 s albo przy najbliższym zapisie; zdarzenie „Utracono kartę SD”. Pomiar, alarmy, wyjścia i wykres 24 h (pamięć PSRAM) działają dalej. Pomiary z czasu bez karty nie trafiają do logów; może przepaść ostatni wiersz zapisywany w chwili wyjęcia. Po włożeniu karta montuje się sama (do 30 s).<br><b>Uszkodzona partycja lub karta niesformatowana:</b> karta traktowana jak brak karty, ponowne próby co 30 s. Urządzenie nie formatuje karty samo – sformatuj ją w komputerze jako FAT32.<br><b>Pełna karta:</b> stare logi są kasowane według ustawień rotacji (dni albo &lt; 10 % wolnego miejsca).<br>Brak karty może włączać usterkę zbiorczą (Ustawienia → Wyjścia).</div><div class="btns"><button class="btn sec" id="sdt">Testuj kartę</button></div><div class="live" id="sdr" style="display:none"></div></div>`;
 $('sdt').onclick=async()=>{$('sdr').style.display='';$('sdr').textContent='Testuję…';const r=await post('/api/v1/sd/test');
  $('sdr').innerHTML=`<b>${esc(r.result)}</b><br>Pin wykrywania karty (GPIO5): <b>${r.cdPin}</b><br>Montowanie: <b>${r.mounted?'OK ('+r.freqMHz+' MHz, '+r.mountMs+' ms)':'błąd ('+r.mountMs+' ms)'}</b>`+(r.mounted?`<br>Typ: <b>${r.type}</b><br>Karta: <b>${r.cardMB} MB</b><br>System plików: <b>${r.totalMB} MB</b>, zajęte <b>${r.usedMB} MB</b><br>Zapis: <b>${r.write?'OK':'BŁĄD'}</b><br>Odczyt: <b>${r.read?'OK':'BŁĄD'}</b> (${r.rwMs} ms)`:'')};
 $('lgs').onclick=()=>save({logs:{intervalMin:+V('lgi'),retentionDays:+V('lgr')}});
 $('lgc').onclick=async()=>{if(confirm('Usunąć WSZYSTKIE logi, dziennik zdarzeń i statystyki z karty SD? Tej operacji nie można cofnąć.')){await post('/api/v1/logs/clear');toast('Usunięto logi')}}};

SET.mqtt=el=>{const m=C.mqtt;el.innerHTML=`<div class="card" style="max-width:640px"><h2>MQTT</h2><div class="live">Stan: <b>${S&&S.mqtt?'połączono':esc(S&&S.mqttMsg||'brak połączenia')}</b></div>
 ${F.chk('mqe','MQTT włączone',m.en)}<div class="g2">${F.txt('mqh','Broker (adres)',m.host,'192.168.1.10')}${F.num('mqp','Port',m.port,1)}${F.txt('mqu','Użytkownik',m.user)}${F.pw('mqw','Hasło',m.hasPass?'•••••• (bez zmian)':'')}</div>
 ${F.txt('mqx','Prefiks tematów',m.prefix)}${F.num('mqi','Wysyłanie co [s]',m.intervalS,1)}${F.chk('mqc','Przyjmuj komendy (kasowanie alarmu, wyjścia ręczne)',m.cmd)}
 <p class="muted" style="font-size:12px">Tematy: ${esc(m.prefix)}/zbiornik1/poziom_cm, …/procent, …/stan, …/litry, …/zuzycie_dzis, ${esc(m.prefix)}/alarm, ${esc(m.prefix)}/status (online/offline). Komendy: ${esc(m.prefix)}/cmd/kasuj, ${esc(m.prefix)}/cmd/out/N.</p>
 <div class="btns"><button class="btn" id="mqs">Zapisz</button></div></div>`;
 $('mqs').onclick=()=>{const o={en:B('mqe'),host:V('mqh'),port:+V('mqp'),user:V('mqu'),prefix:V('mqx'),intervalS:+V('mqi'),cmd:B('mqc')};if($('mqw').value)o.pass=$('mqw').value;save({mqtt:o})}};

SET.modbus=el=>{const m=C.modbus;el.innerHTML=`<div class="card" style="max-width:640px"><h2>Modbus TCP</h2><div class="live">Połączeni klienci: <b>${S?S.mb:0}</b></div>
 ${F.chk('mbe','Serwer Modbus TCP włączony',m.en)}<div class="g2">${F.num('mbp','Port',m.port,1)}${F.num('mbu','Adres urządzenia (Unit ID)',m.unit,1)}</div>
 ${F.chk('mbw','Zezwól na zapis (cewki i rejestry holding)',m.write)}${F.txt('mba','Dozwolone adresy IP (oddzielone przecinkami, puste = wszystkie)',(m.allow||[]).join(', '))}
 <p class="muted" style="font-size:12px">Modbus nie ma haseł – zapis włączaj tylko w zaufanej sieci. Kalibracji nie da się zmienić przez Modbus.</p>
 <div class="btns"><button class="btn" id="mbs">Zapisz</button><a class="btn sec" href="/api/v1/modbus/map.csv">Pobierz mapę rejestrów (CSV)</a></div></div>`;
 $('mbs').onclick=()=>save({modbus:{en:B('mbe'),port:+V('mbp'),unit:+V('mbu'),write:B('mbw'),allow:V('mba').split(/[ ,;]+/).filter(x=>x)}})};

SET.api=async el=>{const j=await (await api('/api/v1/tokens')).json();const P=['odczyt','odczyt + sterowanie','konfiguracja'];
 el.innerHTML=`<div class="card"><h2>Tokeny API</h2><table><tr><th>Nazwa</th><th>Uprawnienia</th><th>Utworzony</th><th>Ważny do</th><th>IP</th><th>Ostatnie użycie</th><th></th></tr>`+
 (j.tokens.length?j.tokens.map(t=>`<tr><td>${esc(t.name)}</td><td>${P[t.perm]}</td><td>${tstr(t.created)}</td><td>${t.expires?tstr(t.expires):'bez terminu'}</td><td>${t.ip||'dowolny'}</td>
 <td>${t.lastUsed?tstr(t.lastUsed)+' ('+t.lastIp+')':'—'}</td><td style="white-space:nowrap"><button class="btn sec sm" onclick="tokRen(${t.id})">Odnów</button> <button class="btn red sm" onclick="tokRev(${t.id})">Odwołaj</button></td></tr>`).join(''):'<tr><td colspan="7" class="muted">Brak tokenów</td></tr>')+`</table>
 <h2 style="margin-top:18px;font-size:15px">Nowy token</h2><div class="g2">${F.txt('tkn','Nazwa (np. SCADA)','')}${F.sel('tkp','Uprawnienia','0',[[0,P[0]],[1,P[1]],[2,P[2]]])}
 ${F.num('tkd','Ważność [dni] (0 = bez terminu)',0,1)}${F.txt('tki','Tylko z adresu IP (opcjonalnie)','')}</div><div class="btns"><button class="btn" id="tkg">Generuj token</button></div></div>
 <div class="card" style="margin-top:16px"><h2>Ustawienia i użycie</h2>${F.chk('alr','Odczyt bez tokenu z sieci lokalnej (status, logi)',j.localRead)}<div class="btns"><button class="btn" id="als">Zapisz</button></div>
 <h2 style="margin-top:18px;font-size:15px">Jak używać API</h2>
 <p class="muted" style="font-size:13px">Adres: <code>http://${location.host}/api/v1/…</code>. Token wygeneruj wyżej i wysyłaj w nagłówku <code>Authorization: Bearer &lt;token&gt;</code>. Odpowiedzi w JSON, błędy jako <code>{"error":"opis"}</code> (401 brak tokenu, 403 za małe uprawnienia, 429 za dużo zapytań – limit 10/s).</p>
 <table><tr><th>Uprawnienie</th><th>Zapytanie</th><th>Co zwraca / robi</th></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/status</code></td><td>poziom (cm, %, litry), prąd mA, stan, alarm, wyjścia, wejścia, sieć, czas, karta SD</td></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/history</code></td><td>ostatnie 24 h co 5 min (do wykresu)</td></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/logs?from=RRRR-MM-DD&amp;to=RRRR-MM-DD</code></td><td>pomiary z karty SD (JSON)</td></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/logs/csv?from=…&amp;to=…</code></td><td>pomiary jako plik CSV (separator ;)</td></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/stats</code></td><td>zużycie dzienne (do 30 dni)</td></tr>
 <tr><td>odczyt</td><td><code>GET /api/v1/events</code></td><td>dziennik zdarzeń</td></tr>
 <tr><td>odczyt + sterowanie</td><td><code>POST /api/v1/alarm/ack</code></td><td>kasowanie alarmu (gdy kasowanie ręczne)</td></tr>
 <tr><td>odczyt + sterowanie</td><td><code>POST /api/v1/outputs {"n":4,"value":60}</code></td><td>wyjście w trybie „ręczne”, 0–100 %</td></tr>
 <tr><td>konfiguracja</td><td><code>POST /api/v1/config {"tanks":[{"thrMin":70}]}</code></td><td>zmiana progów i ustawień (sekcje tanks, alarm, logs)</td></tr></table>
 <p class="muted" style="font-size:13px;margin-top:10px">Przykłady (wiersz poleceń):</p>
 <div class="mono">curl -H "Authorization: Bearer ma_…" http://${location.host}/api/v1/status</div>
 <div class="mono">curl -H "Authorization: Bearer ma_…" -o logi.csv "http://${location.host}/api/v1/logs/csv?from=2026-11-01&amp;to=2026-11-11"</div>
 <div class="mono">curl -X POST -H "Authorization: Bearer ma_…" http://${location.host}/api/v1/alarm/ack</div>
 <p class="muted" style="font-size:12px">Kody stanu (<code>state</code>): 0 NORMALNY, 1 MINIMALNY, 2 KRYTYCZNY, 3 ZA DUŻY, 4 BŁĄD CZUJNIKA, 5 WYŁĄCZONY, 6 NAGŁY SPADEK. Hasło, tokeny, kopia ustawień i aktualizacja wymagają zalogowania na stronie. Połączenie HTTP nie jest szyfrowane – tylko sieć lokalna.</p></div>`;
 $('tkg').onclick=async()=>{const r=await post('/api/v1/tokens',{name:V('tkn'),perm:+V('tkp'),days:+V('tkd'),ip:V('tki')});
  modal('Nowy token API',`<p>Skopiuj token teraz – <b>nie będzie można go ponownie wyświetlić</b>.</p><div class="mono" id="tkv">${r.token}</div><div class="btns"><button class="btn sec" onclick="navigator.clipboard.writeText('${r.token}');toast('Skopiowano')">Kopiuj</button></div>`);settingsShow('api')};
 $('als').onclick=()=>save({api:{localRead:B('alr')}})};
window.tokRev=async id=>{if(confirm('Odwołać token? Integracje używające go przestaną działać.')){await post('/api/v1/tokens/revoke',{id});toast('Odwołano token');settingsShow('api')}};
window.tokRen=async id=>{if(!confirm('Wygenerować nowy token w miejsce starego? Stary przestanie działać.'))return;const r=await post('/api/v1/tokens/revoke',{id,renew:true});
 modal('Odnowiony token',`<p>Nowy token (skopiuj teraz):</p><div class="mono">${r.token}</div><div class="btns"><button class="btn sec" onclick="navigator.clipboard.writeText('${r.token}');toast('Skopiowano')">Kopiuj</button></div>`);settingsShow('api')};

SET.konto=el=>{el.innerHTML=`<div class="grid"><div class="card"><h2>Login i hasło</h2>${F.txt('ku','Login',C.user)}${F.pw('kop','Obecne hasło')}${F.pw('kn1','Nowe hasło (min. 6 znaków)')}${F.pw('kn2','Powtórz nowe hasło')}
 <div class="btns"><button class="btn" id="kps">Zmień</button></div><div class="err" id="kpe"></div></div>
 <div class="card"><h2>Sesja</h2>${F.num('kse','Wylogowanie po bezczynności [min]',C.device.sessionMin,1)}<div class="btns"><button class="btn" id="kss">Zapisz</button></div>
 <p class="muted" style="font-size:12px">Zapomniane hasło: przytrzymaj przycisk DOWNLOAD na płytce przez 10 s (odliczanie na OLED) – login i hasło wrócą do admin/admin, a sieć do ustawień fabrycznych. Kalibracja zostaje.</p></div></div>`;
 $('kps').onclick=async()=>{$('kpe').textContent='';if($('kn1').value!==$('kn2').value){$('kpe').textContent='Hasła nie są takie same';return}
  const r=await fetch('/api/v1/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({user:V('ku'),old:$('kop').value,new:$('kn1').value})});const j=await r.json();
  if(!r.ok){$('kpe').textContent=j.error;return}toast('Zmieniono login/hasło');cfgLoad()};
 $('kss').onclick=()=>save({device:{sessionMin:+V('kse')}})};

SET.system=el=>{const RR={1:'włączenie zasilania',2:'przycisk RESET',3:'restart programowy',4:'błąd programu',5:'watchdog (przerwania)',6:'watchdog zadania',7:'watchdog',9:'spadek napięcia',11:'USB'};
 el.innerHTML=`<div class="grid"><div class="card"><h2>Urządzenie</h2>${F.txt('sn','Nazwa urządzenia',C.device.name)}${F.num('sod','Przyciemnij OLED po [min] bez alarmu (0 = nigdy)',C.device.oledDimMin,1)}
 <div class="btns"><button class="btn" id="sns">Zapisz</button></div><div class="live" id="sinf"></div></div>
 <div class="card"><h2>Aktualizacja programu (OTA)</h2><p class="muted" style="font-size:13px">Wybierz plik <b>02_Monitor_App.ino.bin</b> (folder build). Po wgraniu urządzenie uruchomi się ponownie. Przy błędzie zostaje dotychczasowa wersja.</p>
 <input type="file" id="ofile" accept=".bin"><div class="btns"><button class="btn" id="oup">Wgraj</button></div><div id="oprog" class="muted"></div></div>
 <div class="card"><h2>Kopia ustawień</h2>${F.chk('bks','Dołącz hasła WiFi, AP i MQTT (kopia zaszyfrowana hasłem)',false)}<div class="g2">${F.pw('bkp','Hasło kopii (min. 8 znaków)')}${F.pw('bkp2','Powtórz hasło')}</div><div class="btns"><button class="btn sec" id="bkd">Pobierz kopię</button></div>
 <label>Wgraj kopię (plik .json)</label><input type="file" id="rfile" accept=".json">${F.pw('rpw','Hasło kopii (tylko dla kopii z hasłami)')}<div class="btns"><button class="btn" id="rup">Wgraj kopię</button></div>
 <p class="muted" style="font-size:12px">Kopia nie zawiera loginu, hasła ani tokenów API.</p></div>
 <div class="card"><h2>Restart i ustawienia fabryczne</h2><div class="btns"><button class="btn sec" id="srb">Uruchom ponownie</button></div>
 ${F.chk('fkc','Zachowaj kalibrację, progi i ustawienia zbiorników',true)}<div class="btns"><button class="btn red" id="sfr">Przywróć ustawienia fabryczne</button></div></div></div>`;
 window.onStatus=s=>{const e=$('sinf');if(e)e.innerHTML=`Oprogramowanie: <b>${s.fw}</b><br>Płytka: <b>${esc(s.hw||'')}</b><br>Wolna pamięć: <b>${Math.round(s.heap/1024)} kB</b><br>PSRAM: <b>${Math.round(s.psram/1024)} kB</b><br>
  Ostatni restart: <b>${RR[s.reset]||s.reset}</b><br>Temp. płytki (DS3231): <b>${fc(s.rtcTemp,1)} °C</b><br>ADS1115: <b>${s.ads?'OK':'BŁĄD'}</b><br>PCF8575: <b>${s.pcf?'OK':'BŁĄD'}</b><br>SD: <b>${s.sd.ok?'OK':'brak'}</b>`};if(S)window.onStatus(S);
 $('sns').onclick=()=>save({device:{name:V('sn'),oledDimMin:+V('sod')}});
 $('oup').onclick=()=>{const f=$('ofile').files[0];if(!f){toast('Wybierz plik .bin',1);return}if(!confirm('Wgrać nowy program '+f.name+'?'))return;
  const fd=new FormData();fd.append('file',f,f.name);const x=new XMLHttpRequest();x.open('POST','/api/v1/ota');
  x.upload.onprogress=e=>$('oprog').textContent='Wysyłanie: '+Math.round(e.loaded/e.total*100)+' %';
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}if(x.status==200)rebootWait('Aktualizacja wgrana. Urządzenie uruchamia się ponownie.');else{$('oprog').textContent='';toast(j.error||'Błąd aktualizacji',1)}};
  x.onerror=()=>toast('Błąd połączenia',1);x.send(fd)};
 $('bkd').onclick=async()=>{if(!B('bks')){location='/api/v1/backup';return}const p=V('bkp');if(p.length<8){toast('Hasło kopii: min. 8 znaków',1);return}if(p!==V('bkp2')){toast('Hasła kopii są różne',1);return}
  toast('Szyfrowanie kopii…');const r=await api('/api/v1/backup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pass:p})});if(!r.ok){let j={};try{j=await r.json()}catch(e){}toast(j.error||'Błąd kopii',1);return}
  const b=await r.blob(),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=(r.headers.get('Content-Disposition')||'').match(/filename="([^"]+)"/)?.[1]||'monitorup_ustawienia.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),5000);$('bkp').value=$('bkp2').value=''};
 $('rup').onclick=async()=>{const f=$('rfile').files[0];if(!f){toast('Wybierz plik kopii',1);return}let o;try{o=JSON.parse(await f.text())}catch(e){toast('Plik nie jest poprawnym JSON',1);return}
  if(o.enc){const p=V('rpw');if(!p){toast('Ta kopia jest zaszyfrowana – podaj jej hasło',1);return}o._pass=p}
  if(!confirm('Wgrać ustawienia z pliku i uruchomić urządzenie ponownie?'))return;const rr=await api('/api/v1/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});if(!rr.ok){let j={};try{j=await rr.json()}catch(e){}toast(j.error||'Błąd wgrywania kopii',1);return}rebootWait('Wgrano kopię ustawień.')};
 $('srb').onclick=async()=>{if(confirm('Uruchomić urządzenie ponownie?')){await post('/api/v1/reboot');rebootWait()}};
 $('sfr').onclick=async()=>{if(confirm('Przywrócić ustawienia fabryczne? Login/hasło wrócą do admin/admin, sieć do ustawień domyślnych.')){await post('/api/v1/factory',{keepCal:B('fkc')});rebootWait('Przywrócono ustawienia fabryczne. Zaloguj się: admin / admin.')}}};

// ---------- start ----------
async function chpw(a,b,err){err.textContent='';if(a!==b){err.textContent='Hasła nie są takie same';return false}
 const r=await fetch('/api/v1/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({new:a})});const j=await r.json();if(!r.ok){err.textContent=j.error||'Błąd';return false}return true}
$('mb').onclick=async()=>{if(await chpw($('m1').value,$('m2').value,$('me')))$('mc').style.display='none'};
api('/api/v1/session').then(r=>r.json()).then(j=>{if(j.mustChange)$('mc').style.display='flex'});
onhashchange();tick();setInterval(tick,1500);histLoad().then(tick);setInterval(histLoad,60000);
)JS";

static const char MODBUS_CSV[] PROGMEM = R"CSV(typ;adres;adres_SCADA;nazwa;typ_danych;jednostka_opis
FC04 rejestr wejsciowy;0;30001;Wersja programu;UINT16;20 = 0.2.0
FC04 rejestr wejsciowy;1;30002;Status urzadzenia;UINT16 bity;b0 SD, b1 ADS1115, b2 RTC, b3 czas OK, b4 Ethernet, b5 WiFi, b6 MQTT, b7 alarm aktywny, b8 alarm skasowany, b9 test wyjsc
FC04 rejestr wejsciowy;2-3;30003;Czas pracy;UINT32;s
FC04 rejestr wejsciowy;4;30005;Rok;UINT16;czas lokalny
FC04 rejestr wejsciowy;5;30006;Miesiac;UINT16;1-12
FC04 rejestr wejsciowy;6;30007;Dzien;UINT16;1-31
FC04 rejestr wejsciowy;7;30008;Godzina;UINT16;0-23
FC04 rejestr wejsciowy;8;30009;Minuta;UINT16;0-59
FC04 rejestr wejsciowy;9;30010;Sekunda;UINT16;0-59
FC04 rejestr wejsciowy;10;30011;Alarm zbiorczy (najgorszy stan);UINT16;kody stanu
FC04 rejestr wejsciowy;11;30012;Stan wyjsc OUT1-8;UINT16 bity;b0=OUT1
FC04 rejestr wejsciowy;12;30013;Stan wejsc DI1-8;UINT16 bity;b0=DI1
FC04 rejestr wejsciowy;13;30014;Wolne miejsce SD;UINT16;MB
FC04 rejestr wejsciowy;14;30015;Temperatura DS3231;INT16;C x10
FC04 rejestr wejsciowy;100 / 200;30101 / 30201;Poziom (zbiornik 1 / 2);INT16;cm x10
FC04 rejestr wejsciowy;101 / 201;30102 / 30202;Wypelnienie;INT16;% x10
FC04 rejestr wejsciowy;102 / 202;30103 / 30203;Prad przetwornika;INT16;mA x100
FC04 rejestr wejsciowy;103 / 203;30104 / 30204;Stan;UINT16;0 NORMALNY 1 MINIMALNY 2 KRYTYCZNY 3 ZA DUZY 4 BLAD CZUJNIKA 5 WYLACZONY 6 NAGLY SPADEK
FC04 rejestr wejsciowy;104 / 204;30105 / 30205;Flagi;UINT16 bity;b0 skasowany, b1 pomiar wazny, b2 kalibracja
FC04 rejestr wejsciowy;105 / 205;30106 / 30206;Trend;INT16;cm/h x10
FC04 rejestr wejsciowy;106 / 206;30107 / 30207;Surowy odczyt ADC;INT16;ADS1115
FC04 rejestr wejsciowy;107 / 207;30108 / 30208;Napiecie wejscia;INT16;mV
FC04 rejestr wejsciowy;108-109 / 208-209;30109 / 30209;Poziom;FLOAT32 ABCD;cm
FC04 rejestr wejsciowy;110-111 / 210-211;30111 / 30211;Wypelnienie;FLOAT32 ABCD;%
FC04 rejestr wejsciowy;112-113 / 212-213;30113 / 30213;Prad;FLOAT32 ABCD;mA
FC04 rejestr wejsciowy;114 / 214;30115 / 30215;Zuzycie dzis;INT16;cm x10
FC04 rejestr wejsciowy;115 / 215;30116 / 30216;Zuzycie wczoraj;INT16;cm x10
FC04 rejestr wejsciowy;116 / 216;30117 / 30217;Srednie zuzycie 7 dni;INT16;cm/dobe x10
FC04 rejestr wejsciowy;117 / 217;30118 / 30218;Srednie zuzycie 30 dni;INT16;cm/dobe x10
FC04 rejestr wejsciowy;118 / 218;30119 / 30219;Prognoza wystarczy na;UINT16;dni x10, 65535 = brak danych
FC04 rejestr wejsciowy;119 / 219;30120 / 30220;Ostatnie tankowanie;INT16;cm x10
FC04 rejestr wejsciowy;120-121 / 220-221;30121 / 30221;Objetosc;UINT32;l (0xFFFFFFFF = przeliczanie wylaczone)
FC04 rejestr wejsciowy;122-123 / 222-223;30123 / 30223;Pojemnosc calkowita;UINT32;l
FC04 rejestr wejsciowy;124-125 / 224-225;30125 / 30225;Zuzycie dzis;FLOAT32 ABCD;l
FC04 rejestr wejsciowy;126-127 / 226-227;30127 / 30227;Srednie zuzycie 7 dni;FLOAT32 ABCD;l/dobe
FC02 wejscie dyskretne;0-7;10001-10008;DI1-DI8;BIT;1 = styk zwarty
FC02 wejscie dyskretne;8;10009;Alarm aktywny;BIT;
FC02 wejscie dyskretne;9;10010;Alarm skasowany;BIT;
FC02 wejscie dyskretne;10-13;10011-10014;Z1 KRYTYCZNY / MINIMALNY / ZA DUZY / BLAD CZUJNIKA;BIT;
FC02 wejscie dyskretne;14-17;10015-10018;Z2 KRYTYCZNY / MINIMALNY / ZA DUZY / BLAD CZUJNIKA;BIT;
FC02 wejscie dyskretne;18;10019;Karta SD OK;BIT;
FC02 wejscie dyskretne;19;10020;Czas zsynchronizowany;BIT;
FC02 wejscie dyskretne;20-21;10021-10022;Z1 / Z2 NAGLY SPADEK;BIT;
FC02 wejscie dyskretne;22-23;10023-10024;Z1 / Z2 prognoza ponizej N dni;BIT;
FC01/05/15 cewka;0-7;00001-00008;OUT1-OUT8;BIT;zapis tylko dla wyjsc w trybie reczne
FC01/05/15 cewka;100;00101;Kasuj alarm;BIT;zapis 1 = kwitowanie
FC01/05/15 cewka;101;00102;Test wyjsc;BIT;zapis 1 = uruchom test
FC03/06/16 rejestr holding;0-7;40001-40008;Jasnosc PWM OUT1-8 przed skasowaniem;UINT16;0-100 %
FC03/06/16 rejestr holding;8-15;40009-40016;Jasnosc PWM OUT1-8 po skasowaniu;UINT16;0-100 %
FC03/06/16 rejestr holding;16;40017;Interwal zapisu logu;UINT16;1-60 min
FC03/06/16 rejestr holding;100 / 200;40101 / 40201;Prog KRYTYCZNY;INT16;cm x10
FC03/06/16 rejestr holding;101 / 201;40102 / 40202;Prog MINIMALNY;INT16;cm x10
FC03/06/16 rejestr holding;102 / 202;40103 / 40203;Prog ZA DUZY;INT16;cm x10
FC03/06/16 rejestr holding;103 / 203;40104 / 40204;Histereza;INT16;cm x10
FC03/06/16 rejestr holding;104 / 204;40105 / 40205;Opoznienie alarmu;UINT16;s
FC03/06/16 rejestr holding;105 / 205;40106 / 40206;Wysokosc zbiornika;INT16;cm x10
FC03/06/16 rejestr holding;106 / 206;40107 / 40207;Nagly spadek: prog Y;INT16;cm x10
FC03/06/16 rejestr holding;107 / 207;40108 / 40208;Nagly spadek: czas T;UINT16;min
FC03/06/16 rejestr holding;108 / 208;40109 / 40209;Prog tankowania;INT16;cm x10
FC03/06/16 rejestr holding;109 / 209;40110 / 40210;Ostrzezenie prognozy;UINT16;dni
)CSV";
