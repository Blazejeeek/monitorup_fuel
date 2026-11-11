// MonitorUp - serwer WWW: logowanie (sesja w ciasteczku) i tokeny API (Bearer), API JSON /api/v1, strony
#include "web.h"
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "cfgjson.h"
#include "sensors.h"
#include "logger.h"
#include "timesync.h"
#include "stats.h"
#include "outputs.h"
#include "net.h"
#include "display.h"
#include "hw.h"
#include <SD.h>
#include <ETH.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include "modbus.h"
#include "mqtt.h"
#include "crypt.h"
#include "summary.h"
#include "web_pages.h"

WebServer server(80);
static uint32_t rebootAt = 0;
void scheduleReboot(uint32_t ms) { rebootAt = millis() + ms; }

enum AuthLevel : uint8_t { A_READ = 0, A_CTRL = 1, A_CFG = 2, A_ADMIN = 3 };

// ---------------- Sesje ----------------
#define MAX_SESSIONS 6
struct Session { char id[33]; uint32_t last; bool used; };
static Session sessions[MAX_SESSIONS];
static uint8_t  loginFails = 0;
static uint32_t lockUntil = 0;
static int8_t   curToken = -1;            // token biezacego zapytania (-1 = sesja)

static String cookieSid() {
  String c = server.header("Cookie");
  int p = c.indexOf("MA_SID=");
  if (p < 0) return "";
  int e = c.indexOf(';', p);
  return c.substring(p + 7, e < 0 ? c.length() : e);
}

static Session *findSession() {
  String sid = cookieSid();
  if (sid.length() != 32) return nullptr;
  for (auto &s : sessions) {
    if (!s.used || sid != s.id) continue;
    if (millis() - s.last > (uint32_t)cfg.sessionMin * 60000UL) { s.used = false; return nullptr; }
    s.last = millis();
    return &s;
  }
  return nullptr;
}

static Session *newSession() {
  Session *slot = &sessions[0];
  for (auto &s : sessions) {
    if (!s.used) { slot = &s; break; }
    if (s.last < slot->last) slot = &s;
  }
  uint8_t r[16];
  esp_fill_random(r, sizeof(r));
  for (int i = 0; i < 16; i++) sprintf(slot->id + i * 2, "%02x", r[i]);
  slot->last = millis();
  slot->used = true;
  return slot;
}

// ---------------- Tokeny API ----------------
static void tokHash(const String &tok, uint8_t out[32]) {
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, (const uint8_t *)tok.c_str(), tok.length());
  mbedtls_sha256_finish(&c, out); mbedtls_sha256_free(&c);
}

struct BadIp { uint32_t ip; uint8_t n; uint32_t until; };
static BadIp badIps[4];
static uint32_t tokRateSec[8]; static uint8_t tokRateCnt[8];

static bool ipBlocked(uint32_t ip) {
  for (auto &b : badIps) if (b.ip == ip && b.until && (int32_t)(b.until - millis()) > 0) return true;
  return false;
}
static void ipBad(uint32_t ip) {
  BadIp *s = &badIps[0];
  for (auto &b : badIps) { if (b.ip == ip) { s = &b; break; } if (!b.ip) s = &b; }
  if (s->ip != ip) { s->ip = ip; s->n = 0; s->until = 0; }
  if (++s->n >= 5) { s->until = millis() + 300000; s->n = 0; }
}
static bool isPrivate(IPAddress a) { return a[0] == 10 || (a[0] == 192 && a[1] == 168) || (a[0] == 172 && (a[1] & 0xF0) == 16); }

// ---------------- Pomocnicze ----------------
static void secHeaders() {
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
}
static void sendJson(int code, JsonDocument &d) {
  secHeaders();
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}
static void sendErr(int code, const char *msg) { JsonDocument d; d["error"] = msg; sendJson(code, d); }
static void sendOk() { JsonDocument d; d["ok"] = true; sendJson(200, d); }
static bool parseBody(JsonDocument &d) {
  if (deserializeJson(d, server.arg("plain"))) { sendErr(400, "Nieprawidłowe dane"); return false; }
  return true;
}

static bool auth(uint8_t level) {
  curToken = -1;
  if (findSession()) return true;
  uint32_t rip = (uint32_t)server.client().remoteIP();
  String h = server.header("Authorization");
  if (h.startsWith("Bearer ")) {
    if (ipBlocked(rip)) { sendErr(429, "Za dużo błędnych prób – adres zablokowany na 5 min"); return false; }
    uint8_t hh[32];
    tokHash(h.substring(7), hh);
    time_t now = time(nullptr);
    for (int8_t k = 0; k < 8; k++) {
      ApiToken &t = cfg.tok[k];
      if (!t.used || memcmp(t.hash, hh, 32)) continue;
      if (t.expires && now > (time_t)t.expires) { sendErr(401, "Token wygasł"); return false; }
      if (t.allowIp && t.allowIp != rip) { sendErr(403, "Adres IP niedozwolony dla tokenu"); return false; }
      if (level == A_ADMIN || t.perm < level) { sendErr(403, "Brak uprawnień tokenu"); return false; }
      uint32_t sec = millis() / 1000;
      if (tokRateSec[k] != sec) { tokRateSec[k] = sec; tokRateCnt[k] = 0; }
      if (++tokRateCnt[k] > 10) { sendErr(429, "Za dużo zapytań"); return false; }
      t.lastUsed = now; t.lastIp = rip;
      curToken = k;
      return true;
    }
    ipBad(rip);
    sendErr(401, "Nieprawidłowy token");
    return false;
  }
  if (level == A_READ && cfg.apiLocalRead && isPrivate(server.client().remoteIP())) return true;
  sendErr(401, "Wymagane logowanie");
  return false;
}

static String who() { return curToken >= 0 ? String("token „") + cfg.tok[curToken].name + "”" : String("WWW"); }

static void sendPage(const char *ctype, const char *data) {
  secHeaders();
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, ctype, data);
}

static void chunkBegin(const char *ctype, const String &fname = "") {
  if (fname.length()) server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, ctype, "");
}

static float r1(float v) { return roundf(v * 10) / 10; }
static void jnum(JsonVariant v, float x, int dec) { if (isnan(x)) v.set(nullptr); else v.set(roundf(x * powf(10, dec)) / powf(10, dec)); }

// ---------------- Logowanie ----------------
static void hLogin() {
  JsonDocument d;
  if (!parseBody(d)) return;
  if (lockUntil && (int32_t)(lockUntil - millis()) > 0) { sendErr(429, "Za dużo błędnych prób. Spróbuj za kilka minut."); return; }
  const char *u = d["user"] | "";
  const char *p = d["pass"] | "";
  if (strcmp(u, cfg.user) != 0 || !pwCheck(p)) {
    if (++loginFails >= 5) { lockUntil = millis() + 5UL * 60000UL; loginFails = 0; logEvent("LOGOWANIE", "Blokada po 5 błędnych próbach z " + server.client().remoteIP().toString()); }
    delay(400);
    sendErr(401, "Nieprawidłowy login lub hasło");
    return;
  }
  loginFails = 0; lockUntil = 0;
  Session *s = newSession();
  logEvent("LOGOWANIE", String("Zalogowano: ") + u + " z " + server.client().remoteIP().toString());
  server.sendHeader("Set-Cookie", String("MA_SID=") + s->id + "; Path=/; HttpOnly; SameSite=Strict");
  JsonDocument r; r["ok"] = true; r["mustChange"] = (bool)cfg.mustChange;
  sendJson(200, r);
}

static void hLogout() {
  Session *s = findSession();
  if (s) s->used = false;
  server.sendHeader("Set-Cookie", "MA_SID=; Path=/; Max-Age=0");
  sendOk();
}

static void hSession() {
  if (!findSession()) { sendErr(401, "Wymagane logowanie"); return; }
  JsonDocument r; r["user"] = cfg.user; r["mustChange"] = (bool)cfg.mustChange;
  sendJson(200, r);
}

static void hPassword() {
  if (!findSession()) { sendErr(401, "Wymagane logowanie"); return; }
  JsonDocument d;
  if (!parseBody(d)) return;
  const char *np = d["new"] | "";
  if (!cfg.mustChange && !pwCheck(d["old"] | "")) { sendErr(403, "Obecne hasło jest nieprawidłowe"); return; }
  if (strlen(np) < 6 || strlen(np) > 64) { sendErr(400, "Hasło musi mieć od 6 do 64 znaków"); return; }
  if (strcmp(np, "admin") == 0) { sendErr(400, "Wybierz inne hasło niż domyślne"); return; }
  const char *nu = d["user"] | "";
  if (*nu) strlcpy(cfg.user, nu, sizeof(cfg.user));
  pwSet(np);
  cfg.mustChange = 0;
  cfgSave();
  logEvent("KONTO", "Zmieniono hasło");
  sendOk();
}

// ---------------- Status ----------------
static void hStatus() {
  if (!auth(A_READ)) return;
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["hw"] = HW_NAME " / HW " HW_VERSION;
  d["name"] = cfg.devName;
  d["t"] = (int64_t)time(nullptr);
  d["timeOk"] = timeOk();
  d["timeSrc"] = timeSource;
  d["up"] = millis() / 1000;
  d["out"] = outState;
  JsonArray ol = d["outLvl"].to<JsonArray>();
  for (uint8_t k = 0; k < 8; k++) ol.add(outLevel[k]);
  JsonArray of = d["outFunc"].to<JsonArray>();
  for (uint8_t k = 0; k < 8; k++) of.add(cfg.out[k].func);
  d["di"] = diState;
  d["test"] = (bool)outTestRun;
  JsonObject a = d["alarm"].to<JsonObject>();
  a["active"] = alarmActive; a["acked"] = alarmAcked; a["auto"] = (bool)cfg.ackAuto; a["warn"] = warnActive; a["since"] = alarmSince;
  JsonObject sd = d["sd"].to<JsonObject>();
  sd["ok"] = sdMounted; sd["present"] = sdMounted;
  if (sdMounted) { sd["totalMB"] = (uint32_t)(SD.totalBytes() >> 20); sd["usedMB"] = (uint32_t)(SD.usedBytes() >> 20); }
  d["ads"] = adsOk;
  d["pcf"] = pcfOk;
  d["fault"] = faultBits();
  d["mqtt"] = mqttConnected();
  d["mqttMsg"] = mqttStatusText();
  d["mb"] = mbClientCount();
  d["rtcTemp"] = rtcInfo.temp;
  JsonArray ta = d["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    TankState s = tankGet(i);
    const TankCal &c = cfg.tank[i];
    const TankExtra &x = cfg.tx[i];
    JsonObject t = ta.add<JsonObject>();
    t["name"] = cfg.tankName[i];
    t["en"] = (bool)c.enabled; t["ch"] = c.channel; t["type"] = c.type;
    t["valid"] = s.valid;
    t["cm"] = r1(s.cm); t["pct"] = r1(s.pct);
    t["mA"] = roundf(s.mA * 100) / 100; t["v"] = roundf(s.volt * 1000) / 1000; t["raw"] = s.raw;
    t["state"] = tankEffState(i); t["lvlState"] = s.state;
    t["height"] = c.heightCm; t["thrCrit"] = c.thrCrit; t["thrMin"] = c.thrMin; t["thrHigh"] = c.thrHigh;
    t["volEn"] = (bool)x.volEn;
    if (x.volEn) { jnum(t["vol"], s.valid ? s.vol : NAN, 0); jnum(t["volTotal"], s.volTotal > 0 ? s.volTotal : tankTotalL(i), 0); }
    float a1 = histGet(i, curMin - 1), a0 = histGet(i, curMin - 61);
    jnum(t["trend"], (!isnan(a1) && !isnan(a0)) ? a1 - a0 : NAN, 1);
    t["statsEn"] = (bool)x.statsEn;
    if (x.statsEn) {
      bool L = x.volEn;
      JsonObject so = t["stats"].to<JsonObject>();
      so["unit"] = L ? "l" : "cm";
      jnum(so["today"], statConsToday(i, L), L ? 0 : 1);
      jnum(so["yesterday"], statYesterday(i, L), L ? 0 : 1);
      jnum(so["avg7"], statAvg(i, 7, L), L ? 0 : 1);
      jnum(so["avg30"], statAvg(i, 30, L), L ? 0 : 1);
      jnum(so["forecast"], statForecast(i), 1);
      so["fcWarn"] = statForecastWarn(i);
      if (st[i].lastRefT) { so["lastRefT"] = st[i].lastRefT; jnum(so["lastRef"], L ? st[i].lastRefL : st[i].lastRefCm, L ? 0 : 1); }
      so["refilling"] = st[i].refilling;
    }
    t["drop"] = st[i].drop;
    if (st[i].drop) jnum(t["dropCm"], st[i].dropAmount, 1);
  }
  JsonObject n = d["net"].to<JsonObject>();
  JsonObject e = n["eth"].to<JsonObject>();
  e["en"] = (bool)cfg.ethEn; e["link"] = ethStarted && ETH.linkUp();
  e["ip"] = ethHasIp() ? ETH.localIP().toString() : "";
  if (ethStarted) { e["mac"] = ETH.macAddress(); e["speed"] = ETH.linkUp() ? ETH.linkSpeed() : 0; }
  if (ethHasIp()) { e["mask"] = ETH.subnetMask().toString(); e["gw"] = ETH.gatewayIP().toString(); e["dns"] = ETH.dnsIP().toString(); }
  JsonObject w = n["wifi"].to<JsonObject>();
  w["en"] = (bool)cfg.wifiEn; w["ssid"] = cfg.wifiSsid;
  w["ip"] = wifiHasIp() ? WiFi.localIP().toString() : "";
  w["rssi"] = wifiHasIp() ? WiFi.RSSI() : 0;
  w["mac"] = WiFi.macAddress();
  if (wifiHasIp()) { w["mask"] = WiFi.subnetMask().toString(); w["gw"] = WiFi.gatewayIP().toString(); w["dns"] = WiFi.dnsIP().toString(); }
  JsonObject ap = n["ap"].to<JsonObject>();
  ap["on"] = apOn; ap["ssid"] = cfg.apSsid;
  ap["ip"] = apOn ? WiFi.softAPIP().toString() : "";
  ap["cl"] = apOn ? WiFi.softAPgetStationNum() : 0;
  d["heap"] = ESP.getFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["reset"] = (int)esp_reset_reason();
  sendJson(200, d);
}

// ---------------- Ustawienia ----------------
static void hConfigGet() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  cfgToJson(d.to<JsonObject>(), false);
  d["user"] = cfg.user;
  sendJson(200, d);
}

static void applyAfterChange(JsonObjectConst r, bool &reboot) {
  reboot = r["ap"].is<JsonObjectConst>() || r["wifi"].is<JsonObjectConst>() || r["eth"].is<JsonObjectConst>();
  if (r["tanks"].is<JsonArrayConst>()) sensorsReset();
  if (r["time"].is<JsonObjectConst>()) timeApply();
  if (r["mqtt"].is<JsonObjectConst>()) mqttApply();
  if (r["modbus"].is<JsonObjectConst>()) mbApply();
}

static void hConfigPost() {
  if (!auth(A_CFG)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  JsonObjectConst r = d.as<JsonObjectConst>();
  if (curToken >= 0)
    for (JsonPairConst p : r)
      if (strcmp(p.key().c_str(), "tanks") && strcmp(p.key().c_str(), "alarm") && strcmp(p.key().c_str(), "logs")) {
        sendErr(403, "Token może zmieniać tylko: tanks, alarm, logs"); return;
      }
  String err = cfgFromJson(r);
  if (err.length()) { sendErr(400, err.c_str()); return; }
  cfgSave();
  bool reboot;
  applyAfterChange(r, reboot);
  String secs;
  for (JsonPairConst p : r) { if (secs.length()) secs += ", "; secs += p.key().c_str(); }
  logEvent("USTAWIENIA", "Zmieniono: " + secs + " (" + who() + ")");
  JsonDocument o; o["ok"] = true; o["reboot"] = reboot;
  sendJson(200, o);
  if (reboot) scheduleReboot(2000);
}

static void hWifiScan() {
  if (!auth(A_ADMIN)) return;
  int n = WiFi.scanComplete();
  JsonDocument d;
  if (n == WIFI_SCAN_FAILED || server.hasArg("start")) {
    if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
    d["running"] = true;
  } else if (n == WIFI_SCAN_RUNNING) {
    d["running"] = true;
  } else {
    d["running"] = false;
    JsonArray a = d["list"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      if (!WiFi.SSID(i).length()) continue;
      bool dup = false;
      for (JsonObject o : a) if (o["ssid"] == WiFi.SSID(i)) { dup = true; break; }
      if (dup) continue;
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["ch"] = WiFi.channel(i);
      o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
  }
  sendJson(200, d);
}

static void hCalibrate() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  uint8_t i = d["tank"] | 0;
  if (i >= TANK_COUNT) { sendErr(400, "Nieprawidłowy zbiornik"); return; }
  const char *pt = d["point"] | "";
  float cm = d["cm"] | 0.0f;
  TankState s = tankGet(i);
  TankCal &c = cfg.tank[i];
  float x = c.type == 0 ? s.mA : s.volt;
  float oldMin = c.minX, oldMax = c.maxX, oldMinCm = c.minCm, oldMaxCm = c.maxCm;
  if (!strcmp(pt, "min")) { c.minX = x; c.minCm = cm; }
  else if (!strcmp(pt, "max")) { c.maxX = x; c.maxCm = cm; }
  else { sendErr(400, "Punkt: min lub max"); return; }
  if (fabsf(c.maxX - c.minX) < 0.05f) {
    c.minX = oldMin; c.maxX = oldMax; c.minCm = oldMinCm; c.maxCm = oldMaxCm;
    sendErr(400, "Odczyty MIN i MAX są zbyt bliskie – zmień poziom w zbiorniku między punktami"); return;
  }
  cfgSave();
  sensorsReset();
  logEvent("KALIBRACJA", String(cfg.tankName[i]) + ": punkt " + pt + " = " + String(x, 3) + (c.type == 0 ? " mA" : " V") + " → " + String(cm, 1) + " cm");
  JsonDocument r; r["ok"] = true; r["x"] = x;
  sendJson(200, r);
}

static void hTime() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  int64_t e = d["epoch"] | 0LL;
  if (e < 1700000000LL) { sendErr(400, "Nieprawidłowy czas"); return; }
  timeSetManual((time_t)e, d["src"] | "przeglądarka");
  sendOk();
}

static void hNtpSync() { if (!auth(A_ADMIN)) return; timeSyncNow(); sendOk(); }

static void hAck() { if (!auth(A_CTRL)) return; alarmAck(curToken >= 0 ? 4 : 1); sendOk(); }


static void hOutputs() {
  if (!auth(A_CTRL)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  if (d["test"] == true) { outputsTestStart(); sendOk(); return; }
  int k = (d["n"] | 0) - 1;
  if (k < 0 || k > 7) { sendErr(400, "Wyjście 1–8"); return; }
  if (cfg.out[k].func != OF_MANUAL) { sendErr(400, "Sterować można tylko wyjściem w trybie „ręczne”"); return; }
  cfg.out[k].manual = constrain((int)(d["value"] | 0), 0, 100);
  sendOk();
}

// ---------------- Logi i statystyki ----------------
static bool parseDate(const String &s, struct tm &tm) {
  memset(&tm, 0, sizeof(tm));
  if (sscanf(s.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return false;
  tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_hour = 12; tm.tm_isdst = -1;
  return true;
}

// Zakres dni [from, to] jako lista sciezek plikow
static std::vector<String> dayFiles(int maxDays = 400) {
  std::vector<String> v;
  struct tm a, b;
  time_t now = time(nullptr);
  if (!parseDate(server.arg("from"), a)) { localtime_r(&now, &a); a.tm_hour = 12; }
  if (!parseDate(server.arg("to"), b)) b = a;
  time_t t0 = mktime(&a), t1 = mktime(&b);
  for (time_t t = t0; t <= t1 && (int)v.size() < maxDays; t += 86400) v.push_back(logPath(t));
  return v;
}

static void hLogs() {
  if (!auth(A_READ)) return;
  if (!sdMounted) { sendErr(503, "Brak karty SD"); return; }
  auto files = dayFiles();
  uint32_t maxPts = server.hasArg("max") ? server.arg("max").toInt() : 1500;
  size_t total = 0;
  for (auto &f : files) { File h = SD.open(f); if (h) { total += h.size(); h.close(); } }
  uint32_t step = max<uint32_t>(1, total / 75 / maxPts);
  chunkBegin("application/json");
  server.sendContent("{\"rows\":[");
  bool first = true;
  uint32_t k = 0;
  String buf;
  for (auto &fn : files) {
    File f = SD.open(fn);
    if (!f) continue;
    LogCols lc = logCols(f.readStringUntil('\n'));
    std::vector<String> v;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (k++ % step) continue;
      splitSemi(l, v);
      struct tm tm = {};
      if (sscanf(l.c_str(), "%d-%d-%d;%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) continue;
      tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
      auto fld = [&](int k2) { if (k2 < 0 || k2 >= (int)v.size() || !v[k2].length()) return String("null"); String x = v[k2]; x.replace(',', '.'); return x; };
      buf += (first ? "[" : ",[") + String((uint32_t)mktime(&tm)) + "," + fld(lc.cm[0]) + "," + fld(lc.cm[1]) + "," + fld(lc.l[0]) + "," + fld(lc.l[1]) + "]";
      first = false;
      if (buf.length() > 1200) { server.sendContent(buf); buf = ""; esp_task_wdt_reset(); }
    }
    f.close();
  }
  buf += "]}";
  server.sendContent(buf);
  server.sendContent("");
}

static void hLogsCsv() {
  if (!auth(A_READ)) return;
  if (!sdMounted) { sendErr(503, "Brak karty SD"); return; }
  auto files = dayFiles();
  String a = server.arg("from"), b = server.arg("to");
  chunkBegin("text/csv; charset=utf-8", "monitorup_" + a + (b.length() && b != a ? "_" + b : "") + ".csv");
  server.sendContent("\xEF\xBB\xBF");   // BOM - Excel rozpozna UTF-8
  uint8_t buf[1024];
  String lastHdr;
  for (auto &fn : files) {
    File f = SD.open(fn);
    if (!f) continue;
    String hdr = f.readStringUntil('\n'); hdr.trim();
    if (hdr != lastHdr) { server.sendContent(hdr + "\r\n"); lastHdr = hdr; }   // naglowek tylko przy zmianie kolumn
    size_t n;
    while ((n = f.read(buf, sizeof(buf))) > 0) { server.sendContent((const char *)buf, n); esp_task_wdt_reset(); }
    f.close();
  }
  server.sendContent("");
}

static void hLogFiles() {
  if (!auth(A_READ)) return;
  JsonDocument d;
  d["sd"] = sdMounted;
  if (sdMounted) {
    d["totalMB"] = (uint32_t)(SD.totalBytes() >> 20); d["usedMB"] = (uint32_t)(SD.usedBytes() >> 20);
    JsonArray a = d["days"].to<JsonArray>();
    File root = SD.open("/logs");
    if (root) {
      for (File y = root.openNextFile(); y; y = root.openNextFile()) {
        if (!y.isDirectory()) continue;
        File yd = SD.open(String("/logs/") + y.name());
        for (File m = yd.openNextFile(); m; m = yd.openNextFile()) {
          if (!m.isDirectory()) continue;
          File md = SD.open(String("/logs/") + y.name() + "/" + m.name());
          for (File f = md.openNextFile(); f; f = md.openNextFile()) {
            String n = f.name();
            if (n.endsWith(".csv")) { JsonArray e = a.add<JsonArray>(); e.add(n.substring(0, 10)); e.add((uint32_t)f.size()); }
          }
          md.close();
        }
        yd.close();
      }
      root.close();
    }
  }
  sendJson(200, d);
}

static void hEvents() {
  if (!auth(A_READ)) return;
  JsonDocument d;
  JsonArray a = d["events"].to<JsonArray>();
  String m = server.arg("m");
  if (!m.length()) {          // biezace: z pamieci RAM (najnowsze pierwsze)
    for (int k = 0; k < evCount; k++) {
      EventRec &e = evRam[(evHead + EV_RAM - 1 - k) % EV_RAM];
      JsonArray r = a.add<JsonArray>(); r.add(e.t); r.add(e.type); r.add(e.msg);
    }
  } else if (sdMounted) {
    File f = SD.open("/events/" + m + ".csv");
    if (f) {
      std::vector<String> lines;
      f.readStringUntil('\n');
      while (f.available()) { lines.push_back(f.readStringUntil('\n')); if (lines.size() > 400) lines.erase(lines.begin()); }
      f.close();
      for (int k = lines.size() - 1; k >= 0; k--) {
        String &l = lines[k];
        int p1 = l.indexOf(';'), p2 = l.indexOf(';', p1 + 1), p3 = l.indexOf(';', p2 + 1);
        if (p3 < 0) continue;
        struct tm tm = {};
        sscanf(l.c_str(), "%d-%d-%d;%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
        JsonArray r = a.add<JsonArray>(); r.add((uint32_t)mktime(&tm)); r.add(l.substring(p2 + 1, p3)); r.add(l.substring(p3 + 1));
      }
    }
  }
  sendJson(200, d);
}

static void hStats() {
  if (!auth(A_READ)) return;
  JsonDocument d;
  JsonArray ta = d["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    JsonObject t = ta.add<JsonObject>();
    bool L = cfg.tx[i].volEn;
    t["name"] = cfg.tankName[i]; t["en"] = (bool)cfg.tx[i].statsEn; t["unit"] = L ? "l" : "cm";
    JsonArray days = t["days"].to<JsonArray>();
    for (uint8_t k = 0; k < st[i].nDays; k++) {
      const DayRec &r = st[i].days[k];
      JsonArray e = days.add<JsonArray>();
      e.add(r.day); jnum(e.add<JsonVariant>(), L ? r.consL : r.consCm, L ? 0 : 1); jnum(e.add<JsonVariant>(), L ? r.refL : r.refCm, L ? 0 : 1); e.add(r.full);
    }
    jnum(t["today"], statConsToday(i, L), L ? 0 : 1);
  }
  sendJson(200, d);
}

static void hHistory() {        // ostatnie 24 h co 5 min (wykres na pulpicie)
  if (!auth(A_READ)) return;
  JsonDocument d;
  d["end"] = curMin ? (curMin - 1) * 60 : 0;
  d["step"] = 300;
  JsonArray ta = d["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    JsonArray a = ta.add<JsonArray>();
    if (!curMin) continue;
    for (int k = 287; k >= 0; k--) {
      double s = 0; int n = 0;
      for (int q = 0; q < 5; q++) { float v = histGet(i, curMin - 1 - k * 5 - q); if (!isnan(v)) { s += v; n++; } }
      if (n) a.add(roundf(s / n * 10) / 10); else a.add(nullptr);
    }
  }
  sendJson(200, d);
}

static void rmTree(const String &p) {
  File d = SD.open(p);
  if (!d) return;
  if (!d.isDirectory()) { d.close(); SD.remove(p); return; }
  std::vector<String> items;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) items.push_back(p + "/" + f.name());
  d.close();
  for (auto &i : items) { rmTree(i); esp_task_wdt_reset(); }
  SD.rmdir(p);
}

static void hSdTest() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  sdTest(d.to<JsonObject>());
  sendJson(200, d);
}

static void hLogsClear() {
  if (!auth(A_ADMIN)) return;
  if (!sdMounted) { sendErr(503, "Brak karty SD"); return; }
  rmTree("/logs"); rmTree("/events"); rmTree("/stats");
  for (auto &s : st) s.nDays = 0;
  logEvent("SD", "Usunięto wszystkie logi i statystyki");
  sendOk();
}

// ---------------- Tokeny API ----------------
static void hTokens() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  JsonArray a = d["tokens"].to<JsonArray>();
  for (uint8_t k = 0; k < 8; k++) {
    ApiToken &t = cfg.tok[k];
    if (!t.used) continue;
    JsonObject o = a.add<JsonObject>();
    o["id"] = k; o["name"] = t.name; o["perm"] = t.perm; o["created"] = t.created; o["expires"] = t.expires;
    o["ip"] = ip2s(t.allowIp); o["lastUsed"] = t.lastUsed; o["lastIp"] = ip2s(t.lastIp);
  }
  d["localRead"] = (bool)cfg.apiLocalRead;
  sendJson(200, d);
}

static String tokCreate(uint8_t k) {
  uint8_t r[20];
  esp_fill_random(r, sizeof(r));
  String tok = "ma_";
  for (auto b : r) { char h[3]; sprintf(h, "%02x", b); tok += h; }
  tokHash(tok, cfg.tok[k].hash);
  cfg.tok[k].created = time(nullptr);
  cfg.tok[k].lastUsed = 0; cfg.tok[k].lastIp = 0;
  return tok;
}

static void hTokenNew() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  int k = -1;
  for (int i = 0; i < 8; i++) if (!cfg.tok[i].used) { k = i; break; }
  if (k < 0) { sendErr(400, "Maksymalnie 8 tokenów – odwołaj nieużywany"); return; }
  const char *name = d["name"] | "";
  if (!*name) { sendErr(400, "Podaj nazwę tokenu"); return; }
  uint32_t ip;
  if (!s2ip(d["ip"] | "", ip)) { sendErr(400, "Nieprawidłowy adres IP"); return; }
  ApiToken &t = cfg.tok[k];
  memset(&t, 0, sizeof(t));
  t.used = 1;
  strlcpy(t.name, name, sizeof(t.name));
  t.perm = constrain((int)(d["perm"] | 0), 0, 2);
  int days = d["days"] | 0;
  t.allowIp = ip;
  String tok = tokCreate(k);
  t.expires = days > 0 ? t.created + days * 86400UL : 0;
  cfgSave();
  logEvent("API", String("Utworzono token „") + t.name + "”");
  JsonDocument r; r["ok"] = true; r["token"] = tok; r["id"] = k;
  sendJson(200, r);
}

static void hTokenRevoke() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  int k = d["id"] | -1;
  if (k < 0 || k > 7 || !cfg.tok[k].used) { sendErr(400, "Nie ma takiego tokenu"); return; }
  bool renew = d["renew"] == true;
  String name = cfg.tok[k].name;
  if (renew) {
    String tok = tokCreate(k);
    cfgSave();
    logEvent("API", "Odnowiono token „" + name + "”");
    JsonDocument r; r["ok"] = true; r["token"] = tok;
    sendJson(200, r);
    return;
  }
  memset(&cfg.tok[k], 0, sizeof(ApiToken));
  cfgSave();
  logEvent("API", "Odwołano token „" + name + "”");
  sendOk();
}

// ---------------- Kopia ustawien, system, OTA ----------------
static void hBackup() {
  if (!auth(A_ADMIN)) return;
  if (server.arg("secrets") == "1") { sendErr(400, "Kopia z hasłami tylko zaszyfrowana – użyj hasła kopii"); return; }
  JsonDocument d;
  cfgToJson(d.to<JsonObject>(), false, true);
  String out; serializeJsonPretty(d, out);
  char fn[48]; fmtTime(fn, sizeof(fn), "monitorup_ustawienia_%Y-%m-%d.json");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + fn + "\"");
  secHeaders();
  server.send(200, "application/json", out);
}
// POST {pass}: kopia z haslami WiFi/AP/MQTT, zaszyfrowana haslem kopii (AES-256-GCM, PBKDF2-SHA256)
static void hBackupEnc() {
  if (!auth(A_ADMIN)) return;
  JsonDocument b;
  if (!parseBody(b)) return;
  String pass = b["pass"] | "";
  JsonDocument d;
  cfgToJson(d.to<JsonObject>(), true, true);
  String plain; serializeJson(d, plain);
  JsonDocument e;
  JsonObject o = e.to<JsonObject>();
  o["_monitorup"] = CFG_VERSION;
  String err = backupEncrypt(plain, pass, o);
  plain = "";
  if (err.length()) { sendErr(400, err.c_str()); return; }
  String out; serializeJsonPretty(e, out);
  char fn[56]; fmtTime(fn, sizeof(fn), "monitorup_ustawienia_hasla_%Y-%m-%d.json");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + fn + "\"");
  secHeaders();
  server.send(200, "application/json", out);
  logEvent("USTAWIENIA", "Pobrano zaszyfrowaną kopię ustawień z hasłami");
}
static bool isBackup(JsonObjectConst o) { return o["_monitorup"].is<int>() || o["_monitorapp"].is<int>(); }
static void hRestore() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  if (!isBackup(d.as<JsonObjectConst>())) { sendErr(400, "To nie jest plik kopii ustawień MonitorUp"); return; }
  JsonDocument plainDoc;
  JsonObjectConst src = d.as<JsonObjectConst>();
  if (d["enc"].is<const char *>()) {
    String plain;
    String err = backupDecrypt(src, String((const char *)(d["_pass"] | "")), plain);
    if (err.length()) { sendErr(400, err.c_str()); return; }
    DeserializationError de = deserializeJson(plainDoc, plain);
    plain = "";
    if (de || !isBackup(plainDoc.as<JsonObjectConst>())) { sendErr(400, "Nieprawidłowa zawartość kopii"); return; }
    src = plainDoc.as<JsonObjectConst>();
  }
  String err = cfgFromJson(src);
  if (err.length()) { sendErr(400, err.c_str()); return; }
  cfgSave();
  logEvent("USTAWIENIA", "Wgrano kopię ustawień – restart");
  JsonDocument r; r["ok"] = true; r["reboot"] = true;
  sendJson(200, r);
  scheduleReboot(2000);
}

static void hReboot() { if (!auth(A_ADMIN)) return; logEvent("SYSTEM", "Restart z panelu"); sendOk(); scheduleReboot(); }

static void hFactory() {
  if (!auth(A_ADMIN)) return;
  JsonDocument d;
  if (!parseBody(d)) return;
  bool keepCal = d["keepCal"] | true;
  static TankCal tc[TANK_COUNT]; static TankExtra tx[TANK_COUNT]; static char nm[TANK_COUNT][24];
  memcpy(tc, cfg.tank, sizeof(tc)); memcpy(tx, cfg.tx, sizeof(tx)); memcpy(nm, cfg.tankName, sizeof(nm));
  cfgDefaults();
  if (keepCal) { memcpy(cfg.tank, tc, sizeof(tc)); memcpy(cfg.tx, tx, sizeof(tx)); memcpy(cfg.tankName, nm, sizeof(nm)); }
  cfgSave();
  logEvent("SYSTEM", String("Przywrócono ustawienia fabryczne") + (keepCal ? " (kalibracja zachowana)" : ""));
  sendOk();
  scheduleReboot(2000);
}

static bool otaAllowed = false;
static String otaErr;
static uint32_t otaLastData = 0;
static void hOtaUpload() {
  HTTPUpload &u = server.upload();
  // watchdog kasowany tylko przy realnym postepie; brak danych 15 s = przerwanie (klient zerwal polaczenie)
  if (u.status == UPLOAD_FILE_START || (u.status == UPLOAD_FILE_WRITE && u.currentSize > 0)) { otaLastData = millis(); esp_task_wdt_reset(); }
  if (millis() - otaLastData > 15000) {
    if (otaAllowed) { Update.abort(); otaAllowed = false; otaErr = "Przerwano – brak danych"; }
    server.client().stop();
    return;
  }
  if (u.status == UPLOAD_FILE_START) {
    otaAllowed = findSession() != nullptr;
    otaErr = "";
    if (!otaAllowed) { otaErr = "Wymagane logowanie"; return; }
    if (!u.filename.endsWith(".bin")) { otaErr = "Wybierz plik .bin"; otaAllowed = false; return; }
    int cmd = server.arg("type") == "fs" ? U_SPIFFS : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) { otaErr = Update.errorString(); otaAllowed = false; return; }
    logEvent("OTA", "Start aktualizacji: " + u.filename);
  } else if (u.status == UPLOAD_FILE_WRITE && otaAllowed) {
    if (Update.write(u.buf, u.currentSize) != u.currentSize) { otaErr = Update.errorString(); otaAllowed = false; Update.abort(); }
  } else if (u.status == UPLOAD_FILE_END && otaAllowed) {
    if (!Update.end(true)) { otaErr = Update.errorString(); otaAllowed = false; }
  } else if (u.status == UPLOAD_FILE_ABORTED) { Update.abort(); otaErr = "Przerwano"; otaAllowed = false; }
}
static void hOtaDone() {
  if (otaErr.length() || !otaAllowed) {
    logEvent("OTA", "Błąd aktualizacji: " + otaErr);
    sendErr(otaErr == "Wymagane logowanie" ? 401 : 400, otaErr.length() ? otaErr.c_str() : "Błąd aktualizacji");
    return;
  }
  logEvent("OTA", "Aktualizacja zakończona – restart");
  JsonDocument r; r["ok"] = true; r["reboot"] = true;
  sendJson(200, r);
  scheduleReboot(1500);
}

// GET /api/v1/summary?view=years|year|month&y=RRRR&m=MM – podsumowania z plikow /stats (miesiac_, rok_, lata)
static void hSummary() {
  if (!auth(A_READ)) return;
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  String err = summaryView(server.arg("view"), server.arg("y").toInt(), server.arg("m").toInt(), o);
  if (err.length()) { sendErr(400, err.c_str()); return; }
  JsonArray ta = o["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    JsonObject t = ta.add<JsonObject>();
    t["name"] = cfg.tankName[i]; t["en"] = (bool)cfg.tank[i].enabled; t["vol"] = (bool)cfg.tx[i].volEn; t["height"] = cfg.tank[i].heightCm;
  }
  sendJson(200, d);
}

static void hModbusMap() {
  if (!auth(A_READ)) return;
  server.sendHeader("Content-Disposition", "attachment; filename=\"monitorup_modbus.csv\"");
  server.send_P(200, "text/csv; charset=utf-8", MODBUS_CSV);
}

void webBegin() {
  const char *hdrs[] = {"Cookie", "Authorization"};
  server.collectHeaders(hdrs, 2);

  server.on("/", HTTP_GET, [] {
    if (findSession()) { server.sendHeader("Location", "/app"); server.send(302); return; }
    sendPage("text/html", LOGIN_HTML);
  });
  server.on("/app", HTTP_GET, [] {
    if (!findSession()) { server.sendHeader("Location", "/"); server.send(302); return; }
    sendPage("text/html", APP_HTML);
  });
  server.on("/style.css", HTTP_GET, [] { sendPage("text/css", STYLE_CSS); });
  server.on("/app.js", HTTP_GET, [] { sendPage("application/javascript", APP_JS); });
  server.on("/logo.svg", HTTP_GET, [] { sendPage("image/svg+xml", LOGO_SVG); });

  server.on("/api/v1/login", HTTP_POST, hLogin);
  server.on("/api/v1/logout", HTTP_POST, hLogout);
  server.on("/api/v1/session", HTTP_GET, hSession);
  server.on("/api/v1/password", HTTP_POST, hPassword);
  server.on("/api/v1/status", HTTP_GET, hStatus);
  server.on("/api/v1/config", HTTP_GET, hConfigGet);
  server.on("/api/v1/config", HTTP_POST, hConfigPost);
  server.on("/api/v1/wifi/scan", HTTP_GET, hWifiScan);
  server.on("/api/v1/calibrate", HTTP_POST, hCalibrate);
  server.on("/api/v1/time", HTTP_POST, hTime);
  server.on("/api/v1/time/sync", HTTP_POST, hNtpSync);
  server.on("/api/v1/alarm/ack", HTTP_POST, hAck);
  server.on("/api/v1/outputs", HTTP_POST, hOutputs);
  server.on("/api/v1/logs", HTTP_GET, hLogs);
  server.on("/api/v1/logs/csv", HTTP_GET, hLogsCsv);
  server.on("/api/v1/logs/files", HTTP_GET, hLogFiles);
  server.on("/api/v1/logs/clear", HTTP_POST, hLogsClear);
  server.on("/api/v1/sd/test", HTTP_POST, hSdTest);
  server.on("/api/v1/events", HTTP_GET, hEvents);
  server.on("/api/v1/stats", HTTP_GET, hStats);
  server.on("/api/v1/history", HTTP_GET, hHistory);
  server.on("/api/v1/tokens", HTTP_GET, hTokens);
  server.on("/api/v1/tokens", HTTP_POST, hTokenNew);
  server.on("/api/v1/tokens/revoke", HTTP_POST, hTokenRevoke);
  server.on("/api/v1/backup", HTTP_GET, hBackup);
  server.on("/api/v1/backup", HTTP_POST, hBackupEnc);
  server.on("/api/v1/restore", HTTP_POST, hRestore);
  server.on("/api/v1/reboot", HTTP_POST, hReboot);
  server.on("/api/v1/factory", HTTP_POST, hFactory);
  server.on("/api/v1/ota", HTTP_POST, hOtaDone, hOtaUpload);
  server.on("/api/v1/modbus/map.csv", HTTP_GET, hModbusMap);
  server.on("/api/v1/summary", HTTP_GET, hSummary);

  server.onNotFound([] {
    if (server.uri().startsWith("/api/")) { sendErr(404, "Nie znaleziono"); return; }
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) { delay(100); ESP.restart(); }
}
