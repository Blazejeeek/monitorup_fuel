#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>KinCony B8M - test peryferiow</title>
<style>
body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:0;background:#f2f4f7;color:#1d2433}
header{background:#1d2433;color:#fff;padding:10px 16px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap}
header h1{font-size:18px;margin:0}
#conn{font-size:13px}
main{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:12px;padding:12px}
.card{background:#fff;border-radius:8px;padding:12px;box-shadow:0 1px 3px rgba(0,0,0,.12)}
.card h2{font-size:15px;margin:0 0 8px}
button{padding:6px 10px;margin:2px;border:1px solid #9aa4b5;border-radius:5px;background:#fff;cursor:pointer;font-size:13px}
button.on{background:#1f9d55;color:#fff;border-color:#1f9d55}
.led{display:inline-block;width:38px;text-align:center;padding:5px 0;margin:2px;border-radius:5px;background:#d5dae2;font-size:12px}
.led.on{background:#f0b400;font-weight:bold}
table{border-collapse:collapse;width:100%;font-size:13px}
td{padding:3px 4px;border-bottom:1px solid #eef0f3}
td.v{text-align:right;font-family:Consolas,monospace}
.ok{color:#1f9d55;font-weight:bold}.bad{color:#c62828;font-weight:bold}
.mono{font-family:Consolas,monospace;font-size:12px;word-break:break-all;background:#f6f7f9;padding:4px;min-height:18px}
input{padding:5px;font-size:13px;border:1px solid #9aa4b5;border-radius:5px}
small{color:#5d6778}
</style></head><body>
<header><h1>KinCony B8M - program testowy v<span id="ver"></span></h1><span id="conn">...</span></header>
<main>
<div class="card"><h2>Wyjscia MOSFET (PCF8575 P0-P7)</h2>
<div id="outs"></div>
<div><button onclick="api('out?ch=all&v=1')">Wszystkie ON</button><button onclick="api('out?ch=all&v=0')">Wszystkie OFF</button>
<button id="chase" onclick="api('chase?on='+(S.chase?0:1))">Test kolejny (chase)</button></div>
<small>Wyjscia dzialaja tylko przy zasilaniu 12/24 V (nie z USB).</small></div>

<div class="card"><h2>Wejscia cyfrowe DI1-DI8 (PCF8575 P8-P15)</h2><div id="dis"></div>
<p><small>Zewrzyj styk wejscia z COM, aby zapalic pole.</small></p>
<h2>Przycisk Download (GPIO0)</h2><div id="btn0"></div></div>

<div class="card"><h2>Wejscia analogowe (ADS1115 @0x48)</h2>
<table id="adc"></table>
<small>A3/A4 (4-20 mA): pokazane jest napiecie na boczniku, wartosc bocznika nieznana - wymaga kalibracji.</small></div>

<div class="card"><h2>Zegar RTC DS3231 (@0x68)</h2>
<table><tr><td>Czas RTC</td><td class="v" id="rtc"></td></tr>
<tr><td>Temperatura ukladu</td><td class="v" id="rtct"></td></tr>
<tr><td>Flaga OSF (utrata zasilania)</td><td class="v" id="osf"></td></tr></table>
<button onclick="setRtc()">Ustaw czas z przegladarki</button></div>

<div class="card"><h2>EEPROM 24C02 (@0x50)</h2>
<button onclick="api('eeprom/test').then(r=>q('ee').textContent=r.result)">Test zapisu/odczytu</button>
<div class="mono" id="ee"></div><small>Test zapisuje bajt 0xFF i przywraca jego poprzednia wartosc.</small></div>

<div class="card"><h2>Wyswietlacz OLED SSD1306 (@0x3C)</h2>
<div>Stan: <span id="oled"></span></div>
<button onclick="api('oled?mode=status')">Ekran statusu</button><button onclick="api('oled?mode=fill')">Wszystkie piksele</button>
<button onclick="api('oled?mode=invert')">Inwersja</button><button onclick="api('oled?mode=clear')">Wygas</button></div>

<div class="card"><h2>Karta SD (SPI)</h2>
<table><tr><td>Wykrycie karty (GPIO5)</td><td class="v" id="sdcd"></td></tr>
<tr><td>Zamontowana</td><td class="v" id="sdm"></td></tr><tr><td>Rozmiar</td><td class="v" id="sds"></td></tr></table>
<button onclick="api('sd/test').then(r=>q('sdr').textContent=r.result)">Test zapisu/odczytu</button><div class="mono" id="sdr"></div></div>

<div class="card"><h2>1-Wire (DS18B20)</h2><table id="ow"></table>
<button onclick="api('ow/scan')">Skanuj ponownie</button></div>

<div class="card"><h2>Wolne GPIO (3,3 V!)</h2><table id="gpio"></table>
<small>Wejscie z wewnetrznym podciaganiem. Nie podawac 5 V ani 12/24 V.</small></div>

<div class="card"><h2>RS485 (RX GPIO38 / TX GPIO39)</h2>
<div>Predkosc: <input id="baud" size="6" value="9600"> <input id="rsd" size="14" value="TEST B8M">
<button onclick="rsSend()">Wyslij</button><button onclick="api('rs485/clear')">Wyczysc</button></div>
<div>Odebrane (ASCII):</div><div class="mono" id="rsa"></div><div>Odebrane (HEX):</div><div class="mono" id="rsh"></div></div>

<div class="card"><h2>Modul Tuya (UART GPIO17/16, 9600)</h2>
<div><input id="th" size="24" value="55 AA 03 00 00 01 00 03"><button onclick="api('tuya/send?hex='+encodeURIComponent(q('th').value))">Wyslij HEX</button>
<button onclick="api('tuya/clear')">Wyczysc</button></div>
<div>Odebrane (HEX):</div><div class="mono" id="tuya"></div></div>

<div class="card"><h2>Siec</h2><table id="net"></table></div>

<div class="card"><h2>Magistrala I2C (SDA GPIO8 / SCL GPIO18)</h2><div class="mono" id="i2c"></div>
<button onclick="api('scan')">Skanuj</button></div>

<div class="card"><h2>Autotest przy starcie</h2><table id="self"></table></div>
</main>
<script>
let S={};const q=id=>document.getElementById(id);
function api(p){return fetch('/api/'+p).then(r=>r.json()).then(j=>{refresh();return j}).catch(e=>({result:'blad: '+e}))}
function okc(b){return b?'<span class="ok">OK</span>':'<span class="bad">BRAK</span>'}
function setRtc(){const d=new Date();api(`rtc/set?y=${d.getFullYear()}&mo=${d.getMonth()+1}&d=${d.getDate()}&h=${d.getHours()}&mi=${d.getMinutes()}&s=${d.getSeconds()}`)}
function rsSend(){api('rs485/send?baud='+q('baud').value+'&data='+encodeURIComponent(q('rsd').value))}
function render(s){S=s;q('ver').textContent=s.fw;
let h='';for(let i=0;i<8;i++){h+=`<button class="${s.out[i]?'on':''}" onclick="api('out?ch=${i+1}&v=t')">OUT${i+1}</button>`}q('outs').innerHTML=h;
q('chase').className=s.chase?'on':'';
h='';for(let i=0;i<8;i++){h+=`<span class="led ${s.di[i]?'on':''}">DI${i+1}</span>`}q('dis').innerHTML=h+(s.pcf?'':' <span class="bad">PCF8575 nie odpowiada</span>');
q('btn0').innerHTML=`<span class="led ${s.btn0?'on':''}">${s.btn0?'WCISN.':'puszcz.'}</span>`;
const an=['A1 (0-5 V)','A2 (0-5 V)','A3 (4-20 mA)','A4 (4-20 mA)'];
h='';for(let i=0;i<4;i++){h+=`<tr><td>${an[i]}</td><td class="v">${s.ads?s.adc[i].toFixed(4)+' V':'-'}</td><td class="v">${s.ads?s.raw[i]:''}</td></tr>`}
q('adc').innerHTML=h+(s.ads?'':'<tr><td colspan=3 class="bad">ADS1115 nie odpowiada</td></tr>');
q('rtc').textContent=s.rtc.ok?s.rtc.time:'brak';q('rtct').textContent=s.rtc.ok?s.rtc.temp.toFixed(2)+' °C':'-';
q('osf').innerHTML=s.rtc.ok?(s.rtc.osf?'<span class="bad">USTAWIONA - ustaw czas / sprawdz baterie</span>':'<span class="ok">OK</span>'):'-';
q('oled').innerHTML=okc(s.oled);
q('sdcd').textContent='poziom '+s.sd.cd;q('sdm').innerHTML=okc(s.sd.mounted);q('sds').textContent=s.sd.mounted?s.sd.sizeMB+' MB':'-';
h='';s.ow.forEach(o=>{h+=`<tr><td>GPIO${o.pin}</td><td>${o.n} czujn.</td><td class="v">${o.t.map(t=>t>-100?t.toFixed(2)+' °C':'err').join(', ')||'-'}</td></tr>`});q('ow').innerHTML=h;
h='';s.gpio.forEach(g=>{h+=`<tr><td>GPIO${g.pin}</td><td>${g.out?'WYJSCIE':'wejscie'}</td><td class="v">${g.lvl}</td><td>
<button onclick="api('gpio?pin=${g.pin}&mode=in')">WE</button><button class="${g.out&&g.lvl?'on':''}" onclick="api('gpio?pin=${g.pin}&mode=out&v=1')">H</button><button class="${g.out&&!g.lvl?'on':''}" onclick="api('gpio?pin=${g.pin}&mode=out&v=0')">L</button></td></tr>`});q('gpio').innerHTML=h;
q('rsa').textContent=s.rs485.ascii;q('rsh').textContent=s.rs485.hex;q('tuya').textContent=s.tuya;
q('net').innerHTML=`<tr><td>WiFi AP SSID</td><td class="v">${s.net.ssid}</td></tr><tr><td>IP AP</td><td class="v">${s.net.apip}</td></tr>
<tr><td>Klienci AP</td><td class="v">${s.net.clients}</td></tr><tr><td>mDNS</td><td class="v">${s.net.mdns}</td></tr>
<tr><td>Ethernet W5500</td><td class="v">${s.net.eth?'zainicjowany':'<span class="bad">blad</span>'}</td></tr>
<tr><td>Ethernet link</td><td class="v">${s.net.link?s.net.speed+' Mbps':'brak kabla'}</td></tr><tr><td>Ethernet IP (DHCP)</td><td class="v">${s.net.ethip}</td></tr>
<tr><td>Ethernet MAC</td><td class="v">${s.net.ethmac}</td></tr><tr><td>Czas pracy</td><td class="v">${s.up} s</td></tr>
<tr><td>Wolny heap / PSRAM</td><td class="v">${s.heap} / ${s.psram} B</td></tr>`;
q('i2c').textContent=s.i2c||'brak urzadzen';
h='';s.self.forEach(t=>{h+=`<tr><td>${t[0]}</td><td class="v ${t[1]?'ok':'bad'}">${t[1]?'OK':'BLAD'}</td><td>${t[2]}</td></tr>`});q('self').innerHTML=h;}
function refresh(){fetch('/api/status').then(r=>r.json()).then(s=>{render(s);q('conn').textContent='polaczono '+new Date().toLocaleTimeString()}).catch(()=>q('conn').textContent='brak polaczenia')}
refresh();setInterval(refresh,1000);
</script></body></html>)rawliteral";
