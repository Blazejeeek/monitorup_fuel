// MonitorUp - serwer Modbus TCP (mapa rejestrow: PLAN.md sekcja 12, MODBUS.md)
#include "modbus.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "sensors.h"
#include "stats.h"
#include "outputs.h"
#include "timesync.h"
#include "net.h"
#include "mqtt.h"
#include "logger.h"
#include <SD.h>

#define FW_NUM 41            // wersja programu w rejestrze 0 (30 = 0.3.0)
#define MB_MAX_CLIENTS 4

static WiFiServer *mbServer = nullptr;
static WiFiClient mbCli[MB_MAX_CLIENTS];
static bool mbSaveDirty = false;
static uint32_t mbSaveT = 0;
uint8_t mbClientCount() { uint8_t n = 0; for (auto &c : mbCli) if (c && c.connected()) n++; return n; }

static uint16_t i16s(float v, float scale, bool valid = true) {
  if (!valid || isnan(v)) return 0x8000;
  float r = roundf(v * scale);
  if (r > 32767) r = 32767;
  if (r < -32767) r = -32767;
  return (uint16_t)(int16_t)r;
}
static uint16_t fHi(float v) { uint32_t u; memcpy(&u, &v, 4); return u >> 16; }
static uint16_t fLo(float v) { uint32_t u; memcpy(&u, &v, 4); return u & 0xFFFF; }

static bool mbInput(uint16_t a, uint16_t &v) {
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
  if (a < 100) {
    switch (a) {
      case 0: v = FW_NUM; return true;
      case 1: {
        v = 0;
        if (sdMounted) v |= 1; if (adsOk) v |= 2; if (rtcInfo.ok) v |= 4; if (timeOk()) v |= 8;
        if (ethHasIp()) v |= 16; if (wifiHasIp()) v |= 32; if (mqttConnected()) v |= 64;
        if (alarmActive) v |= 128; if (alarmAcked) v |= 256; if (outTestRun) v |= 512;
        return true;
      }
      case 2: v = (millis() / 1000) >> 16; return true;
      case 3: v = (millis() / 1000) & 0xFFFF; return true;
      case 4: v = tm.tm_year + 1900; return true;
      case 5: v = tm.tm_mon + 1; return true;
      case 6: v = tm.tm_mday; return true;
      case 7: v = tm.tm_hour; return true;
      case 8: v = tm.tm_min; return true;
      case 9: v = tm.tm_sec; return true;
      case 10: {
        static const uint8_t rank[] = {0, 2, 5, 4, 1, 0, 6};   // priorytet stanow
        uint8_t worst = 0, wr = 0;
        for (uint8_t i = 0; i < TANK_COUNT; i++) { uint8_t e = tankEffState(i); if (rank[e] > wr) { wr = rank[e]; worst = e; } }
        v = worst; return true;
      }
      case 11: v = outState; return true;
      case 12: v = diState; return true;
      case 13: v = sdMounted ? (uint16_t)min<uint64_t>((SD.totalBytes() - SD.usedBytes()) >> 20, 65535) : 0; return true;
      case 14: v = i16s(rtcInfo.temp, 10, rtcInfo.ok); return true;
      default: v = 0; return a < 20;
    }
  }
  uint8_t t = a / 100 - 1;
  if (t >= TANK_COUNT || a % 100 >= 30) return false;
  uint16_t o = a % 100;
  TankState s = tankGet(t);
  bool L = cfg.tx[t].volEn;
  float fc = statForecast(t);
  switch (o) {
    case 0: v = i16s(s.cm, 10, s.valid); break;
    case 1: v = i16s(s.pct, 10, s.valid); break;
    case 2: v = i16s(s.mA, 100); break;
    case 3: v = tankEffState(t); break;
    case 4: v = (alarmAcked ? 1 : 0) | (s.valid ? 2 : 0) | 4; break;
    case 5: {
      float a1 = histGet(t, curMin - 1), a0 = histGet(t, curMin - 61);
      v = i16s(a1 - a0, 10, !isnan(a1) && !isnan(a0)); break;
    }
    case 6: v = (uint16_t)s.raw; break;
    case 7: v = i16s(s.volt * 1000, 1); break;
    case 8: v = fHi(s.valid ? s.cm : NAN); break;   case 9: v = fLo(s.valid ? s.cm : NAN); break;
    case 10: v = fHi(s.valid ? s.pct : NAN); break; case 11: v = fLo(s.valid ? s.pct : NAN); break;
    case 12: v = fHi(s.mA); break;                  case 13: v = fLo(s.mA); break;
    case 14: v = i16s(statConsToday(t, false), 10); break;
    case 15: v = i16s(statYesterday(t, false), 10); break;
    case 16: v = i16s(statAvg(t, 7, false), 10); break;
    case 17: v = i16s(statAvg(t, 30, false), 10); break;
    case 18: v = isnan(fc) ? 0xFFFF : (uint16_t)min(fc * 10, 65534.0f); break;
    case 19: v = i16s(st[t].lastRefCm, 10); break;
    case 20: case 21: { uint32_t u = (L && s.valid) ? (uint32_t)s.vol : 0xFFFFFFFF; v = o == 20 ? u >> 16 : u & 0xFFFF; break; }
    case 22: case 23: { uint32_t u = L ? (uint32_t)tankTotalL(t) : 0xFFFFFFFF; v = o == 22 ? u >> 16 : u & 0xFFFF; break; }
    case 24: v = fHi(L ? statConsToday(t, true) : NAN); break; case 25: v = fLo(L ? statConsToday(t, true) : NAN); break;
    case 26: v = fHi(L ? statAvg(t, 7, true) : NAN); break;    case 27: v = fLo(L ? statAvg(t, 7, true) : NAN); break;
    default: v = 0;
  }
  return true;
}

static bool mbDiscrete(uint16_t a, bool &v) {
  if (a < 8) { v = diState >> a & 1; return true; }
  auto e = [](uint8_t i) { return tankEffState(i); };
  switch (a) {
    case 8: v = alarmActive; return true;
    case 9: v = alarmAcked; return true;
    case 10: v = e(0) == ST_CRIT; return true;  case 11: v = e(0) == ST_MIN; return true;
    case 12: v = e(0) == ST_HIGH; return true;  case 13: v = e(0) == ST_FAULT; return true;
    case 14: v = e(1) == ST_CRIT; return true;  case 15: v = e(1) == ST_MIN; return true;
    case 16: v = e(1) == ST_HIGH; return true;  case 17: v = e(1) == ST_FAULT; return true;
    case 18: v = sdMounted; return true;
    case 19: v = timeOk(); return true;
    case 20: v = st[0].drop; return true;  case 21: v = st[1].drop; return true;
    case 22: v = statForecastWarn(0); return true;  case 23: v = statForecastWarn(1); return true;
  }
  return false;
}

static bool mbCoilRead(uint16_t a, bool &v) {
  if (a < 8) { v = outState >> a & 1; return true; }
  if (a == 100 || a == 101) { v = false; return true; }
  return false;
}

static bool mbCoilWrite(uint16_t a, bool v) {
  if (a < 8) {
    if (cfg.out[a].func != OF_MANUAL) return false;
    cfg.out[a].manual = v ? 100 : 0;
    return true;
  }
  if (a == 100) { if (v) alarmAck(2); return true; }
  if (a == 101) { if (v) outputsTestStart(); return true; }
  return false;
}

// Rejestry holding zmiennoprzecinkowe (x10): 0 krytyczny, 1 minimalny, 2 za duzy, 3 histereza, 5 wysokosc, 6 spadek Y, 8 tankowanie
static bool holdFloat(uint8_t t, uint8_t o, float *get, const float *set) {
  TankCal &c = cfg.tank[t];
  TankExtra &x = cfg.tx[t];
  float v;
  switch (o) {
    case 0: v = c.thrCrit; if (set) c.thrCrit = *set; break;
    case 1: v = c.thrMin; if (set) c.thrMin = *set; break;
    case 2: v = c.thrHigh; if (set) c.thrHigh = *set; break;
    case 3: v = c.hyst; if (set) c.hyst = *set; break;
    case 5: v = c.heightCm; if (set) c.heightCm = *set; break;
    case 6: v = x.dropCm; if (set) x.dropCm = *set; break;
    case 8: v = x.refillCm; if (set) x.refillCm = *set; break;
    default: return false;
  }
  if (get) *get = v;
  return true;
}

static bool mbHoldRead(uint16_t a, uint16_t &v) {
  if (a < 8) { v = cfg.out[a].pwmPre; return true; }
  if (a < 16) { v = cfg.out[a - 8].pwmPost; return true; }
  if (a == 16) { v = cfg.logIntervalMin; return true; }
  if (a >= 100 && a < 300) {
    uint8_t t = a / 100 - 1, o = a % 100;
    if (o == 4) { v = cfg.tank[t].delayS; return true; }
    if (o == 7) { v = cfg.tx[t].dropMin; return true; }
    if (o == 9) { v = cfg.tx[t].fcWarnDays; return true; }
    float f;
    if (holdFloat(t, o, &f, nullptr)) { v = i16s(f, 10); return true; }
  }
  return false;
}

static bool mbHoldWrite(uint16_t a, uint16_t v) {
  if (a < 16) { if (v > 100) return false; if (a < 8) cfg.out[a].pwmPre = v; else cfg.out[a - 8].pwmPost = v; mbSaveDirty = true; return true; }
  if (a == 16) { if (v < 1 || v > 60) return false; cfg.logIntervalMin = v; mbSaveDirty = true; return true; }
  if (a >= 100 && a < 300) {
    uint8_t t = a / 100 - 1, o = a % 100;
    if (o == 4) { cfg.tank[t].delayS = v; mbSaveDirty = true; return true; }
    if (o == 7) { if (!v) return false; cfg.tx[t].dropMin = v; mbSaveDirty = true; return true; }
    if (o == 9) { cfg.tx[t].fcWarnDays = v; mbSaveDirty = true; return true; }
    float old, nv = (int16_t)v / 10.0f;
    if (!holdFloat(t, o, &old, &nv)) return false;
    const TankCal &c = cfg.tank[t];
    if (!(c.thrCrit < c.thrMin && c.thrMin < c.thrHigh) || c.heightCm <= 0 || c.hyst < 0) { holdFloat(t, o, nullptr, &old); return false; }
    mbSaveDirty = true;
    return true;
  }
  return false;
}

static void mbHandle(WiFiClient &c) {
  uint8_t h[7];
  if (c.available() < 8) return;
  c.read(h, 7);
  uint16_t len = (h[4] << 8) | h[5];
  if (len < 2 || len > 253) { c.stop(); return; }
  uint8_t pdu[253];
  uint32_t t0 = millis();
  size_t got = 0;
  while (got < (size_t)(len - 1) && millis() - t0 < 200) { if (c.available()) got += c.read(pdu + got, len - 1 - got); }
  if (got < (size_t)(len - 1)) { c.stop(); return; }
  uint8_t unit = h[6];
  if (unit != cfg.mbUnit && unit != 0 && unit != 255) return;

  uint8_t fc = pdu[0];
  uint8_t out[260];
  size_t n = 0;
  auto ex = [&](uint8_t code) { out[0] = fc | 0x80; out[1] = code; n = 2; };
  uint16_t addr = (pdu[1] << 8) | pdu[2], qty = (pdu[3] << 8) | pdu[4];
  bool writeFc = fc == 5 || fc == 6 || fc == 15 || fc == 16;
  if (writeFc && !cfg.mbWriteEn) ex(1);
  else switch (fc) {
    case 1: case 2: {
      if (qty < 1 || qty > 2000) { ex(3); break; }
      uint8_t bytes = (qty + 7) / 8;
      out[0] = fc; out[1] = bytes; memset(out + 2, 0, bytes); n = 2 + bytes;
      for (uint16_t k = 0; k < qty; k++) {
        bool v;
        if (!(fc == 1 ? mbCoilRead(addr + k, v) : mbDiscrete(addr + k, v))) { ex(2); break; }
        if (v) out[2 + k / 8] |= 1 << (k % 8);
      }
      break;
    }
    case 3: case 4: {
      if (qty < 1 || qty > 125) { ex(3); break; }
      out[0] = fc; out[1] = qty * 2; n = 2 + qty * 2;
      for (uint16_t k = 0; k < qty; k++) {
        uint16_t v;
        if (!(fc == 3 ? mbHoldRead(addr + k, v) : mbInput(addr + k, v))) { ex(2); break; }
        out[2 + k * 2] = v >> 8; out[3 + k * 2] = v & 0xFF;
      }
      break;
    }
    case 5: {
      if (qty != 0xFF00 && qty != 0) { ex(3); break; }
      if (!mbCoilWrite(addr, qty == 0xFF00)) { ex(2); break; }
      memcpy(out, pdu, 5); n = 5; break;
    }
    case 6: {
      if (!mbHoldWrite(addr, qty)) { ex(3); break; }
      memcpy(out, pdu, 5); n = 5; break;
    }
    case 15: {
      if (qty < 1 || qty > 1968) { ex(3); break; }
      for (uint16_t k = 0; k < qty; k++) if (!mbCoilWrite(addr + k, pdu[6 + k / 8] >> (k % 8) & 1)) { ex(2); break; }
      if (!n) { memcpy(out, pdu, 5); n = 5; }
      break;
    }
    case 16: {
      if (qty < 1 || qty > 123) { ex(3); break; }
      for (uint16_t k = 0; k < qty; k++) if (!mbHoldWrite(addr + k, (pdu[6 + k * 2] << 8) | pdu[7 + k * 2])) { ex(3); break; }
      if (!n) { memcpy(out, pdu, 5); n = 5; }
      break;
    }
    default: ex(1);
  }
  uint8_t frame[7 + 260] = {h[0], h[1], 0, 0, (uint8_t)((n + 1) >> 8), (uint8_t)((n + 1) & 0xFF), unit};
  memcpy(frame + 7, out, n);
  c.write(frame, 7 + n);              // jedna ramka TCP (niektore klienty SCADA tego wymagaja)
}

void mbApply() {
  if (mbServer) { for (auto &c : mbCli) c.stop(); mbServer->end(); delete mbServer; mbServer = nullptr; }
  if (!cfg.mbEn) return;
  mbServer = new WiFiServer(cfg.mbPort);
  mbServer->begin();
  mbServer->setNoDelay(true);
}

static bool mbIpAllowed(IPAddress ip) {
  bool any = false;
  for (auto a : cfg.mbAllowIp) if (a) { any = true; if ((uint32_t)ip == a) return true; }
  return !any;
}

void mbLoop() {
  if (!mbServer) return;
  WiFiClient nc = mbServer->accept();
  if (nc) {
    if (!mbIpAllowed(nc.remoteIP())) nc.stop();
    else {
      bool placed = false;
      for (auto &c : mbCli) if (!c || !c.connected()) { c = nc; placed = true; break; }
      if (!placed) nc.stop();
    }
  }
  for (auto &c : mbCli) if (c && c.connected()) { for (int k = 0; k < 4 && c.available() >= 8; k++) mbHandle(c); }
  if (mbSaveDirty && millis() - mbSaveT > 2000) { mbSaveT = millis(); mbSaveDirty = false; cfgSave(); logEvent("MODBUS", "Zmieniono ustawienia przez Modbus"); }
}
