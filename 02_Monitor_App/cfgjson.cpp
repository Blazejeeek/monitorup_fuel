// Monitor App - ustawienia <-> JSON (strona Ustawienia, kopia ustawien, import)
#include "cfgjson.h"
// Ta sama para funkcji obsluguje odczyt, zapis czesciowy (tylko podane klucze) i kopie zapasowa.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

String ip2s(uint32_t v) { return v ? IPAddress(v).toString() : String(""); }
bool s2ip(const char *s, uint32_t &out) {
  if (!s || !*s) { out = 0; return true; }
  IPAddress a;
  if (!a.fromString(s)) return false;
  out = (uint32_t)a;
  return true;
}

static void netIpToJson(JsonObject o, const NetIp &n) {
  o["dhcp"] = (bool)n.dhcp;
  o["ip"] = ip2s(n.ip); o["mask"] = ip2s(n.mask); o["gw"] = ip2s(n.gw);
  o["dns1"] = ip2s(n.dns1); o["dns2"] = ip2s(n.dns2);
}

// Zwraca komunikat bledu albo "" gdy OK
static String netIpFromJson(JsonObjectConst o, NetIp &n) {
  NetIp t = n;
  if (o["dhcp"].is<bool>()) t.dhcp = o["dhcp"].as<bool>();
  const char *k[] = {"ip", "mask", "gw", "dns1", "dns2"};
  uint32_t *v[] = {&t.ip, &t.mask, &t.gw, &t.dns1, &t.dns2};
  for (int i = 0; i < 5; i++)
    if (o[k[i]].is<const char *>() && !s2ip(o[k[i]], *v[i])) return String("Nieprawidłowy adres: ") + k[i];
  if (!t.dhcp && (!t.ip || !t.mask)) return "Przy adresie statycznym podaj IP i maskę";
  n = t;
  return "";
}

template <size_t N> static void cpyStr(char (&dst)[N], JsonVariantConst v) { if (v.is<const char *>()) strlcpy(dst, v.as<const char *>(), N); }
template <typename T> static T numOr(JsonVariantConst v, T def) { return (v.is<float>() || v.is<int>()) ? v.as<T>() : def; }
static uint8_t boolOr(JsonVariantConst v, uint8_t def) { return v.is<bool>() ? (uint8_t)v.as<bool>() : v.is<int>() ? (uint8_t)(v.as<int>() != 0) : def; }

// secrets = true: dolacz hasla WiFi/AP/MQTT (kopia ustawien), skroty hasla i tokenow nigdy nie wychodza poza kopie
void cfgToJson(JsonObject r, bool secrets, bool backup) {
  JsonObject dev = r["device"].to<JsonObject>();
  dev["name"] = cfg.devName;
  dev["sessionMin"] = cfg.sessionMin;
  dev["oledDimMin"] = cfg.oledDimMin;

  JsonObject ap = r["ap"].to<JsonObject>();
  ap["mode"] = cfg.apMode; ap["ssid"] = cfg.apSsid; ap["channel"] = cfg.apChannel;
  if (secrets) ap["pass"] = cfg.apPass;

  JsonObject w = r["wifi"].to<JsonObject>();
  w["en"] = (bool)cfg.wifiEn; w["ssid"] = cfg.wifiSsid;
  if (secrets) w["pass"] = cfg.wifiPass;
  w["hasPass"] = cfg.wifiPass[0] != 0;
  netIpToJson(w["net"].to<JsonObject>(), cfg.wifiIp);

  JsonObject e = r["eth"].to<JsonObject>();
  e["en"] = (bool)cfg.ethEn;
  netIpToJson(e["net"].to<JsonObject>(), cfg.ethIp);

  JsonArray ta = r["tanks"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    const TankCal &c = cfg.tank[i];
    const TankExtra &x = cfg.tx[i];
    JsonObject t = ta.add<JsonObject>();
    t["name"] = cfg.tankName[i];
    t["en"] = (bool)c.enabled; t["ch"] = c.channel; t["type"] = c.type; t["smooth"] = c.smooth;
    t["shunt"] = c.shuntOhm;
    t["minX"] = c.minX; t["minCm"] = c.minCm; t["maxX"] = c.maxX; t["maxCm"] = c.maxCm;
    t["offset"] = c.offsetCm; t["height"] = c.heightCm;
    t["thrCrit"] = c.thrCrit; t["thrMin"] = c.thrMin; t["thrHigh"] = c.thrHigh;
    t["hyst"] = c.hyst; t["delay"] = c.delayS;
    t["volEn"] = (bool)x.volEn; t["shape"] = x.shape; t["dimA"] = x.dimA; t["dimB"] = x.dimB; t["dimC"] = x.dimC;
    JsonArray tb = t["table"].to<JsonArray>();
    for (uint8_t k = 0; k < x.tblN && k < VOL_TABLE_N; k++) { JsonArray p = tb.add<JsonArray>(); p.add(x.tblCm[k]); p.add(x.tblL[k]); }
    t["statsEn"] = (bool)x.statsEn; t["refill"] = x.refillCm; t["fcRef"] = x.fcRef; t["fcWarn"] = x.fcWarnDays;
    t["dropEn"] = (bool)x.dropEn; t["dropCm"] = x.dropCm; t["dropMin"] = x.dropMin;
    t["nightEn"] = (bool)x.nightEn; t["nightFrom"] = x.nightFrom; t["nightTo"] = x.nightTo; t["nightCm"] = x.nightCm;
  }

  JsonObject al = r["alarm"].to<JsonObject>();
  al["reminderMin"] = cfg.reminderMin; al["ackDi1"] = (bool)cfg.ackDi1En; al["lampOnMin"] = cfg.lampOnMin;
  al["lampGpio"] = cfg.lampHwGpio;
  al["bootTest"] = (bool)cfg.bootTestEn; al["bootMask"] = cfg.bootTestMask; al["bootMs"] = cfg.bootTestMs;
  al["bootSkipSw"] = (bool)cfg.bootTestSkipSw;
  JsonArray oa = al["out"].to<JsonArray>();
  for (auto &o : cfg.out) {
    JsonObject j = oa.add<JsonObject>();
    j["func"] = o.func; j["blink"] = (bool)o.blink; j["onMs"] = o.onMs; j["offMs"] = o.offMs;
    j["pre"] = o.pwmPre; j["post"] = o.pwmPost; j["manual"] = o.manual;
  }

  JsonObject tm = r["time"].to<JsonObject>();
  tm["ntp"] = (bool)cfg.ntpEn; tm["server"] = cfg.ntpServer; tm["server2"] = cfg.ntpServer2; tm["intervalMin"] = cfg.ntpIntervalMin;

  JsonObject lg = r["logs"].to<JsonObject>();
  lg["intervalMin"] = cfg.logIntervalMin; lg["retentionDays"] = cfg.logRetentionDays;

  JsonObject mq = r["mqtt"].to<JsonObject>();
  mq["en"] = (bool)cfg.mqttEn; mq["host"] = cfg.mqttHost; mq["port"] = cfg.mqttPort; mq["user"] = cfg.mqttUser;
  if (secrets) mq["pass"] = cfg.mqttPass;
  mq["hasPass"] = cfg.mqttPass[0] != 0;
  mq["prefix"] = cfg.mqttPrefix; mq["intervalS"] = cfg.mqttIntervalS; mq["ha"] = (bool)cfg.mqttHa; mq["cmd"] = (bool)cfg.mqttCmdEn;

  JsonObject mb = r["modbus"].to<JsonObject>();
  mb["en"] = (bool)cfg.mbEn; mb["port"] = cfg.mbPort; mb["unit"] = cfg.mbUnit; mb["write"] = (bool)cfg.mbWriteEn;
  JsonArray ips = mb["allow"].to<JsonArray>();
  for (auto ip : cfg.mbAllowIp) if (ip) ips.add(ip2s(ip));

  JsonObject api = r["api"].to<JsonObject>();
  api["localRead"] = (bool)cfg.apiLocalRead;

  if (backup) r["_monitorapp"] = CFG_VERSION;
}

// Zapis czesciowy: zmieniane tylko podane klucze. Zwraca "" albo opis bledu (nic nie zapisano).
String cfgFromJson(JsonObjectConst r) {
  static Config old;          // static: struktura ma kilka kB, nie na stosie
  old = cfg;
  String err;
  auto fail = [&](const String &m) { cfg = old; return m; };

  if (r["device"].is<JsonObjectConst>()) {
    JsonObjectConst d = r["device"];
    cpyStr(cfg.devName, d["name"]);
    cfg.sessionMin = numOr(d["sessionMin"], cfg.sessionMin);
    cfg.oledDimMin = numOr(d["oledDimMin"], cfg.oledDimMin);
    if (cfg.sessionMin < 5 || cfg.sessionMin > 1440) return fail("Czas sesji: 5–1440 min");
  }
  if (r["ap"].is<JsonObjectConst>()) {
    JsonObjectConst a = r["ap"];
    cfg.apMode = numOr(a["mode"], cfg.apMode); cpyStr(cfg.apSsid, a["ssid"]); cfg.apChannel = numOr(a["channel"], cfg.apChannel);
    if (a["pass"].is<const char *>() && strlen(a["pass"]) > 0) cpyStr(cfg.apPass, a["pass"]);
    if (cfg.apMode > 2) return fail("Nieprawidłowy tryb AP");
    if (!cfg.apSsid[0]) return fail("Podaj nazwę sieci AP");
    if (strlen(cfg.apPass) < 8) return fail("Hasło AP musi mieć min. 8 znaków");
    if (cfg.apChannel < 1 || cfg.apChannel > 13) return fail("Kanał AP: 1–13");
  }
  if (r["wifi"].is<JsonObjectConst>()) {
    JsonObjectConst w = r["wifi"];
    cfg.wifiEn = boolOr(w["en"], cfg.wifiEn); cpyStr(cfg.wifiSsid, w["ssid"]);
    if (w["pass"].is<const char *>() && (strlen(w["pass"]) > 0 || w["clearPass"] == true)) cpyStr(cfg.wifiPass, w["pass"]);
    if (w["net"].is<JsonObjectConst>() && (err = netIpFromJson(w["net"], cfg.wifiIp)) != "") return fail("WiFi: " + err);
    if (cfg.wifiEn && !cfg.wifiSsid[0]) return fail("Wybierz sieć WiFi");
  }
  if (r["eth"].is<JsonObjectConst>()) {
    JsonObjectConst e = r["eth"];
    cfg.ethEn = boolOr(e["en"], cfg.ethEn);
    if (e["net"].is<JsonObjectConst>() && (err = netIpFromJson(e["net"], cfg.ethIp)) != "") return fail("Ethernet: " + err);
  }
  if (r["tanks"].is<JsonArrayConst>()) {
    uint8_t i = 0;
    for (JsonObjectConst t : r["tanks"].as<JsonArrayConst>()) {
      if (i >= TANK_COUNT) break;
      TankCal &c = cfg.tank[i];
      TankExtra &x = cfg.tx[i];
      cpyStr(cfg.tankName[i], t["name"]);
      c.enabled = boolOr(t["en"], c.enabled); c.channel = numOr(t["ch"], c.channel); c.type = numOr(t["type"], c.type); c.smooth = numOr(t["smooth"], c.smooth);
      c.shuntOhm = numOr(t["shunt"], c.shuntOhm);
      c.minX = numOr(t["minX"], c.minX); c.minCm = numOr(t["minCm"], c.minCm); c.maxX = numOr(t["maxX"], c.maxX); c.maxCm = numOr(t["maxCm"], c.maxCm);
      c.offsetCm = numOr(t["offset"], c.offsetCm); c.heightCm = numOr(t["height"], c.heightCm);
      c.thrCrit = numOr(t["thrCrit"], c.thrCrit); c.thrMin = numOr(t["thrMin"], c.thrMin); c.thrHigh = numOr(t["thrHigh"], c.thrHigh);
      c.hyst = numOr(t["hyst"], c.hyst); c.delayS = numOr(t["delay"], c.delayS);
      x.volEn = boolOr(t["volEn"], x.volEn); x.shape = numOr(t["shape"], x.shape);
      x.dimA = numOr(t["dimA"], x.dimA); x.dimB = numOr(t["dimB"], x.dimB); x.dimC = numOr(t["dimC"], x.dimC);
      if (t["table"].is<JsonArrayConst>()) {
        x.tblN = 0;
        for (JsonArrayConst p : t["table"].as<JsonArrayConst>()) {
          if (x.tblN >= VOL_TABLE_N) break;
          x.tblCm[x.tblN] = p[0].as<float>(); x.tblL[x.tblN] = p[1].as<float>(); x.tblN++;
        }
      }
      x.statsEn = boolOr(t["statsEn"], x.statsEn); x.refillCm = numOr(t["refill"], x.refillCm); x.fcRef = numOr(t["fcRef"], x.fcRef); x.fcWarnDays = numOr(t["fcWarn"], x.fcWarnDays);
      x.dropEn = boolOr(t["dropEn"], x.dropEn); x.dropCm = numOr(t["dropCm"], x.dropCm); x.dropMin = numOr(t["dropMin"], x.dropMin);
      x.nightEn = boolOr(t["nightEn"], x.nightEn); x.nightFrom = numOr(t["nightFrom"], x.nightFrom); x.nightTo = numOr(t["nightTo"], x.nightTo); x.nightCm = numOr(t["nightCm"], x.nightCm);

      String n = String(cfg.tankName[i]) + ": ";
      if (c.channel > 3 || c.type > 1) return fail(n + "nieprawidłowe wejście");
      if (fabsf(c.maxX - c.minX) < 0.01f) return fail(n + "punkty MIN i MAX muszą się różnić");
      if (c.heightCm <= 0) return fail(n + "podaj wysokość zbiornika");
      if (!(c.thrCrit < c.thrMin && c.thrMin < c.thrHigh)) return fail(n + "progi muszą spełniać: krytyczny < minimalny < za duży");
      if (c.thrHigh > c.heightCm * 1.05f) return fail(n + "próg ZA DUŻY większy niż wysokość zbiornika");
      if (c.hyst < 0 || c.hyst > 50) return fail(n + "histereza 0–50 cm");
      if (c.smooth > 100) c.smooth = 100;
      if (x.volEn && x.shape == SH_TABLE && x.tblN < 2) return fail(n + "tabela litrów wymaga min. 2 punktów");
      if (x.volEn && x.shape != SH_TABLE && (x.dimA <= 0 || (x.shape != SH_VCYL && x.dimB <= 0))) return fail(n + "podaj wymiary zbiornika");
      if (x.dropMin < 1) x.dropMin = 1;
      i++;
    }
  }
  if (r["alarm"].is<JsonObjectConst>()) {
    JsonObjectConst a = r["alarm"];
    cfg.reminderMin = numOr(a["reminderMin"], cfg.reminderMin); cfg.ackDi1En = boolOr(a["ackDi1"], cfg.ackDi1En); cfg.lampOnMin = numOr(a["lampOnMin"], cfg.lampOnMin);
    cfg.lampHwGpio = numOr(a["lampGpio"], cfg.lampHwGpio);
    cfg.bootTestEn = boolOr(a["bootTest"], cfg.bootTestEn); cfg.bootTestMask = numOr(a["bootMask"], cfg.bootTestMask); cfg.bootTestMs = numOr(a["bootMs"], cfg.bootTestMs);
    cfg.bootTestSkipSw = boolOr(a["bootSkipSw"], cfg.bootTestSkipSw);
    if (cfg.lampHwGpio && cfg.lampHwGpio != 13 && cfg.lampHwGpio != 14 && cfg.lampHwGpio != 21) return fail("GPIO lampki: 13, 14 lub 21");
    if (cfg.bootTestMs < 100 || cfg.bootTestMs > 5000) return fail("Czas testu wyjścia: 100–5000 ms");
    if (a["out"].is<JsonArrayConst>()) {
      uint8_t k = 0;
      for (JsonObjectConst o : a["out"].as<JsonArrayConst>()) {
        if (k >= 8) break;
        OutCfg &c = cfg.out[k++];
        c.func = numOr(o["func"], c.func); c.blink = boolOr(o["blink"], c.blink); c.onMs = numOr(o["onMs"], c.onMs); c.offMs = numOr(o["offMs"], c.offMs);
        c.pwmPre = numOr(o["pre"], c.pwmPre); c.pwmPost = numOr(o["post"], c.pwmPost); c.manual = numOr(o["manual"], c.manual);
        if (c.func > OF_WARN) return fail("Nieprawidłowa funkcja wyjścia");
        c.pwmPre = min<uint8_t>(c.pwmPre, 100); c.pwmPost = min<uint8_t>(c.pwmPost, 100); c.manual = min<uint8_t>(c.manual, 100);
        if (c.onMs < 100) c.onMs = 100;
        if (c.offMs < 100) c.offMs = 100;
      }
    }
  }
  if (r["time"].is<JsonObjectConst>()) {
    JsonObjectConst t = r["time"];
    cfg.ntpEn = boolOr(t["ntp"], cfg.ntpEn); cpyStr(cfg.ntpServer, t["server"]); cpyStr(cfg.ntpServer2, t["server2"]); cfg.ntpIntervalMin = numOr(t["intervalMin"], cfg.ntpIntervalMin);
    if (cfg.ntpIntervalMin < 5) cfg.ntpIntervalMin = 5;
    if (cfg.ntpEn && !cfg.ntpServer[0]) return fail("Podaj serwer NTP");
  }
  if (r["logs"].is<JsonObjectConst>()) {
    cfg.logIntervalMin = numOr(r["logs"]["intervalMin"], cfg.logIntervalMin); cfg.logRetentionDays = numOr(r["logs"]["retentionDays"], cfg.logRetentionDays);
    if (cfg.logIntervalMin < 1 || cfg.logIntervalMin > 60) return fail("Interwał logu: 1–60 min");
    if (cfg.logRetentionDays < 7) cfg.logRetentionDays = 7;
  }
  if (r["mqtt"].is<JsonObjectConst>()) {
    JsonObjectConst m = r["mqtt"];
    cfg.mqttEn = boolOr(m["en"], cfg.mqttEn); cpyStr(cfg.mqttHost, m["host"]); cfg.mqttPort = numOr(m["port"], cfg.mqttPort); cpyStr(cfg.mqttUser, m["user"]);
    if (m["pass"].is<const char *>() && (strlen(m["pass"]) > 0 || m["clearPass"] == true)) cpyStr(cfg.mqttPass, m["pass"]);
    cpyStr(cfg.mqttPrefix, m["prefix"]); cfg.mqttIntervalS = numOr(m["intervalS"], cfg.mqttIntervalS); cfg.mqttHa = boolOr(m["ha"], cfg.mqttHa); cfg.mqttCmdEn = boolOr(m["cmd"], cfg.mqttCmdEn);
    if (cfg.mqttEn && !cfg.mqttHost[0]) return fail("Podaj adres brokera MQTT");
    if (cfg.mqttIntervalS < 2) cfg.mqttIntervalS = 2;
    size_t L = strlen(cfg.mqttPrefix);
    while (L && cfg.mqttPrefix[L - 1] == '/') cfg.mqttPrefix[--L] = 0;
    if (!L) return fail("Podaj prefiks tematów MQTT");
  }
  if (r["modbus"].is<JsonObjectConst>()) {
    JsonObjectConst m = r["modbus"];
    cfg.mbEn = boolOr(m["en"], cfg.mbEn); cfg.mbPort = numOr(m["port"], cfg.mbPort); cfg.mbUnit = numOr(m["unit"], cfg.mbUnit); cfg.mbWriteEn = boolOr(m["write"], cfg.mbWriteEn);
    if (m["allow"].is<JsonArrayConst>()) {
      memset(cfg.mbAllowIp, 0, sizeof(cfg.mbAllowIp));
      uint8_t k = 0;
      for (JsonVariantConst v : m["allow"].as<JsonArrayConst>()) {
        if (k >= 4) break;
        uint32_t ip;
        if (!s2ip(v.as<const char *>(), ip)) return fail("Modbus: nieprawidłowy adres IP");
        if (ip) cfg.mbAllowIp[k++] = ip;
      }
    }
    if (!cfg.mbPort) return fail("Modbus: podaj port");
  }
  if (r["api"].is<JsonObjectConst>()) cfg.apiLocalRead = boolOr(r["api"]["localRead"], cfg.apiLocalRead);
  return "";
}
