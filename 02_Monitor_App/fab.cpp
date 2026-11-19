// MonitorUp - dane fabryczne egzemplarza (0.5.6, karta #177), opis w fab.h
#include "fab.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_mac.h>
#include "hw.h"
#include "config.h"
#include "sensors.h"
#include "outputs.h"
#include "logger.h"
#include "cloudproto.h"
#include "cloud.h"

void pwHash(const uint8_t salt[16], const char *pw, uint8_t out[32]);   // config.cpp

FabData fab;
FabState fabState = FAB_NONE;

#define FAB_MAGIC 0x4246u          // "FB"
#define FAB_VER   1
// Kopia numeru w EEPROM: [0..1] 'S''N', [2] wersja, [3] dlugosc, [4..19] numer, [20..21] CRC16
// 0..121 kopia kalibracji zbiornikow (config.cpp), 128..229 tabela kalibracji pradu (calib.cpp)
#define EE_SN_BASE 232
#define EE_SN_LEN  (4 + FAB_SN_LEN + 2)
static_assert(EE_SN_BASE + EE_SN_LEN <= 256, "kopia numeru nie miesci sie w 24C02");

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}

static bool eeSnSave(const char *sn) {
  uint8_t b[EE_SN_LEN] = {0};
  b[0] = 'S'; b[1] = 'N'; b[2] = 1; b[3] = (uint8_t)strnlen(sn, FAB_SN_LEN);
  memcpy(b + 4, sn, b[3]);
  uint16_t c = crc16(b, EE_SN_LEN - 2);
  b[EE_SN_LEN - 2] = c & 0xFF; b[EE_SN_LEN - 1] = c >> 8;
  return eeWriteBlock(EE_SN_BASE, b, EE_SN_LEN);
}

static bool eeSnLoad(char *sn) {
  uint8_t b[EE_SN_LEN];
  if (!eeReadBlock(EE_SN_BASE, b, EE_SN_LEN)) return false;
  if (b[0] != 'S' || b[1] != 'N' || b[2] != 1 || b[3] == 0 || b[3] >= FAB_SN_LEN) return false;
  if ((b[EE_SN_LEN - 2] | (b[EE_SN_LEN - 1] << 8)) != crc16(b, EE_SN_LEN - 2)) return false;
  memcpy(sn, b + 4, b[3]); sn[b[3]] = 0;
  return true;
}

static bool valid(const FabData &f) { return f.magic == FAB_MAGIC && f.ver == FAB_VER && f.sn[0] && memchr(f.sn, 0, FAB_SN_LEN); }

void fabLoad() {
  memset(&fab, 0, sizeof(fab));
  Preferences p;
  p.begin("fab", true);
  bool ok = p.getBytesLength("d") == sizeof(fab) && p.getBytes("d", &fab, sizeof(fab)) == sizeof(fab) && valid(fab);
  p.end();
  char ee[FAB_SN_LEN] = {0};
  bool eeOk = eeSnLoad(ee);
  if (ok) {
    fabState = FAB_OK;
    if (!eeOk || strcmp(ee, fab.sn)) {
      bool w = eeSnSave(fab.sn);
      Serial.printf("Dane fabryczne: kopia numeru w EEPROM %s\n", w ? "odtworzona" : "- BLAD zapisu");
    }
    Serial.printf("Dane fabryczne: %s, HW %s, %s\n", fab.sn, fab.hw, fab.date);
    return;
  }
  memset(&fab, 0, sizeof(fab));
  if (eeOk) {
    strlcpy(fab.sn, ee, sizeof(fab.sn));
    fabState = FAB_EE_ONLY;
    Serial.printf("Dane fabryczne: BRAK w pamieci, numer %s z kopii EEPROM - wymagane ponowne zaprogramowanie\n", ee);
  } else {
    fabState = FAB_NONE;
    Serial.println("Dane fabryczne: brak (sterownik niezaprogramowany)");
  }
}

bool fabProgrammed() { return fabState == FAB_OK; }

// 0.6.0: haslo chmury po wymianie (cmd/sys/rotate) - osobny klucz, uklad FabData bez zmian
static char cloudPassCur[33] = {0};
const char *fabCloudPass() {
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    Preferences p;
    if (p.begin("fab", true)) { if (p.isKey("cp")) p.getString("cp", cloudPassCur, sizeof(cloudPassCur)); p.end(); }
  }
  return cloudPassCur[0] ? cloudPassCur : fab.cloudPass;
}
bool fabCloudPassCommit(const char *pw) {
  Preferences p;
  if (!p.begin("fab", false)) return false;
  bool ok = p.putString("cp", pw) == strlen(pw);
  p.end();
  if (ok) strlcpy(cloudPassCur, pw, sizeof(cloudPassCur));
  return ok;
}
String fabHwId() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  return hwFromMac(m).c_str();
}
const char *fabSn() { return fab.sn; }

bool fabCal(CalTable &out) {
  if (!fabProgrammed()) return false;
  Preferences p;
  p.begin("fab", true);
  bool ok = p.getBytesLength("cal") == sizeof(out) && p.getBytes("cal", &out, sizeof(out)) == sizeof(out) && out.n >= 2 && out.n <= CAL_MAX;
  p.end();
  return ok;
}

bool fabIsStartPanelPass(const char *pw) {
  if (!fabProgrammed()) return false;
  uint8_t h[32];
  pwHash(fab.panelSalt, pw, h);
  return memcmp(h, fab.panelHash, 32) == 0;
}

// ---------------- Polecenia z portu szeregowego ----------------
static String hex4(const uint8_t *h) { char b[9]; snprintf(b, sizeof(b), "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]); return b; }
static void sha(const char *s, uint8_t out[32]) { mbedtls_sha256((const uint8_t *)s, strlen(s), out, 0); }
static String chk(const char *s) { uint8_t h[32]; sha(s, h); return hex4(h); }

// znaki drukowalne ASCII bez spacji, dlugosc min..max
static bool okText(const char *s, size_t mn, size_t mx) {
  size_t n = strlen(s);
  if (n < mn || n > mx) return false;
  for (size_t i = 0; i < n; i++) if (s[i] <= ' ' || s[i] > '~') return false;
  return true;
}
// MU-RRMM-NNNN: 2 wielkie litery, myslnik, 4 cyfry, myslnik, 4 cyfry
static bool okSn(const char *s) {
  if (strlen(s) != 12 || s[2] != '-' || s[7] != '-') return false;
  for (int i = 0; i < 12; i++) {
    if (i == 2 || i == 7) continue;
    if (i < 2 ? !(s[i] >= 'A' && s[i] <= 'Z') : !isdigit((uint8_t)s[i])) return false;
  }
  return true;
}

String fabMac() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);   // WiFi.macAddress() zwraca 00:..:00, gdy Wi-Fi nie jest uruchomione (praca na Ethernecie)
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

static void info() {
  JsonDocument d;
  d["mac"] = fabMac();
  d["hw_id"] = fabHwId();                       // 0.6.0: login do chmury Zdalio
  d["cloud"] = cloudStCode();                    // 0.6.0: stan polaczenia z chmura (test na stanowisku)
  d["cloud_en"] = (bool)cfg.cloudEn;
  d["fw"] = FW_VERSION;
  d["board"] = HW_NAME;
  d["programmed"] = fabProgrammed();
  d["state"] = fabState == FAB_OK ? "ok" : fabState == FAB_EE_ONLY ? "tylko_eeprom" : "brak";
  d["sn"] = fab.sn;
  char ee[FAB_SN_LEN] = {0};
  d["ee_sn"] = eeSnLoad(ee) ? ee : "";
  if (fabProgrammed()) {
    d["hw"] = fab.hw; d["date"] = fab.date;
    d["cloud_host"] = fab.cloudHost; d["cloud_port"] = fab.cloudPort;
    JsonObject c = d["chk"].to<JsonObject>();
    c["ap"] = chk(fab.apPass); c["panel"] = hex4(fab.chkPanel); c["cloud"] = chk(fabCloudPass());
    CalTable t;
    d["cal"] = fabCal(t) ? t.n : 0;
    d["cal_active"] = calTab.n;
    d["ap_active"] = strcmp(cfg.apPass, fab.apPass) == 0;
    d["must_change"] = (bool)cfg.mustChange;
  }
  String s; serializeJson(d, s);
  Serial.println("PROV-INFO " + s);
}

static String provision(JsonDocument &d) {
  if (fabProgrammed()) {
    if (!(d["serwis"] | false)) return "sterownik juz zaprogramowany (ponownie tylko z flaga serwisowa)";
    if (strcmp(d["sn_stary"] | "", fab.sn)) return "sn_stary nie zgadza sie z numerem sterownika";
  }
  const char *sn = d["sn"] | "", *hw = d["hw"] | "", *date = d["date"] | "";
  const char *ap = d["ap_pass"] | "", *pp = d["panel_pass"] | "", *cp = d["cloud_pass"] | "", *ch = d["cloud_host"] | "";
  long port = d["cloud_port"] | 8883L;
  if (!okSn(sn)) return "niepoprawny numer seryjny (wzor MU-RRMM-NNNN)";
  if (fabState == FAB_EE_ONLY && strcmp(sn, fab.sn) && !(d["serwis"] | false)) return "w EEPROM jest inny numer seryjny - uzyj flagi serwisowej";
  if (!okText(hw, 1, 7)) return "niepoprawna wersja sprzetu";
  if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return "niepoprawna data (RRRR-MM-DD)";
  if (!okText(ap, 8, 32)) return "haslo Wi-Fi: 8-32 znaki bez spacji";
  if (!okText(pp, 8, 32)) return "haslo panelu: 8-32 znaki bez spacji";
  if (!strcmp(pp, ap)) return "haslo panelu musi byc inne niz haslo Wi-Fi";
  if (!okText(cp, 16, 32)) return "haslo chmury: 16-32 znaki bez spacji";
  if (!okText(ch, 3, 63)) return "niepoprawny adres chmury";
  if (port < 1 || port > 65535) return "niepoprawny port chmury";

  FabData f;
  memset(&f, 0, sizeof(f));
  f.magic = FAB_MAGIC; f.ver = FAB_VER;
  strlcpy(f.sn, sn, sizeof(f.sn)); strlcpy(f.hw, hw, sizeof(f.hw)); strlcpy(f.date, date, sizeof(f.date));
  strlcpy(f.apPass, ap, sizeof(f.apPass));
  esp_fill_random(f.panelSalt, sizeof(f.panelSalt));
  pwHash(f.panelSalt, pp, f.panelHash);
  uint8_t h[32]; sha(pp, h); memcpy(f.chkPanel, h, 4);
  strlcpy(f.cloudPass, cp, sizeof(f.cloudPass)); strlcpy(f.cloudHost, ch, sizeof(f.cloudHost));
  f.cloudPort = (uint16_t)port;
  f.programmed = (uint32_t)time(nullptr);

  Preferences p;
  if (!p.begin("fab", false)) return "blad otwarcia pamieci NVS";
  size_t w = p.putBytes("d", &f, sizeof(f));
  p.remove("cp");                                  // 0.6.0: haslo z wymiany chmury traci waznosc
  p.end();
  cloudPassCur[0] = 0;
  if (w != sizeof(f)) return "blad zapisu NVS";
  if (!eeSnSave(f.sn)) return "zapisano w NVS, ale blad zapisu kopii numeru w EEPROM";
  const bool service = fabProgrammed();
  fab = f; fabState = FAB_OK;
  // haslo startowe panelu (wymuszona zmiana) i siec fabryczna z nowym haslem Wi-Fi
  accountDefaults();
  netDefaults();
  cfgSave();
  logEvent("SYSTEM", String(service ? "Ponowne programowanie serwisowe: " : "Zaprogramowano dane fabryczne: ") + f.sn + ", HW " + f.hw);
  return "";
}

static String provCal(JsonDocument &d) {
  if (!fabProgrammed()) return "najpierw PROVISION";
  CalTable old;
  if (fabCal(old) && !((d["serwis"] | false) && !strcmp(d["sn"] | "", fab.sn))) return "kalibracja fabryczna juz zapisana (ponownie tylko z flaga serwisowa i numerem)";
  String err = calFromJson(d.as<JsonObjectConst>());   // aktywna tabela + kopia EEPROM (przy bledzie calTab bez zmian)
  if (err.length() && !err.startsWith("Zapisano")) return err;   // tylko brak kopii EEPROM jest ostrzezeniem
  Preferences p;
  if (!p.begin("fab", false)) return "blad otwarcia pamieci NVS";
  size_t w = p.putBytes("cal", &calTab, sizeof(calTab));
  p.end();
  if (w != sizeof(calTab)) return "blad zapisu NVS";
  sensorsReset();
  logEvent("KALIBRACJA", String("Fabryczna kalibracja prądu A4: ") + calTab.n + " pkt");
  return err;   // pusty albo ostrzezenie o EEPROM
}

static void meas() {
  TankState s = tankGet(0);
  JsonDocument d;
  d["raw"] = roundf(s.volt * 32768.0f / ADS_FS_V * 10) / 10;
  d["mA"] = roundf(s.mA * 1000) / 1000;
  d["valid"] = s.valid;
  d["ads"] = adsOk;
  d["di"] = diState;
  d["out"] = outState;
  d["pcf"] = pcfOk;
  d["rtc"] = rtcInfo.ok;
  String o; serializeJson(d, o);
  Serial.println("PROV-MEAS " + o);
}

static String line;
static bool tooLong = false;

static void handle() {
  if (line == "PROV-INFO") { info(); return; }
  if (line == "PROV-MEAS") { meas(); return; }
  if (line == "PROV-CLOUDTEST" || line == "PROV-CLOUDTEST 0") {   // 0.6.0: test polaczenia z chmura (5 min, bez zapisu)
    if (line.endsWith(" 0")) { cloudTest(0); Serial.println("PROV-OK chmura test koniec"); return; }
    if (!fabProgrammed()) { Serial.println("PROV-ERR najpierw PROVISION"); return; }
    cloudTest(300); Serial.println("PROV-OK chmura test 300 s"); return;
  }
  if (line == "PROV-OUTTEST") { outputsTestStart(); Serial.println("PROV-OK test wyjsc"); return; }
  bool isProv = line.startsWith("PROVISION "), isCal = line.startsWith("PROV-CAL ");
  if (!isProv && !isCal) return;
  JsonDocument d;
  if (deserializeJson(d, line.c_str() + (isProv ? 10 : 9))) { Serial.println("PROV-ERR niepoprawny JSON"); return; }
  String err = isProv ? provision(d) : provCal(d);
  line = "";                                       // tresc z haslami nie zostaje w pamieci
  if (isProv && err.length()) { Serial.println("PROV-ERR " + err); return; }
  if (isCal && err.length() && !calTab.n) { Serial.println("PROV-ERR " + err); return; }
  Serial.println(String("PROV-OK ") + fab.sn + (err.length() ? " (ostrzezenie: " + err + ")" : String("")));
  if (isProv) { Serial.flush(); delay(300); ESP.restart(); }
}

#define PROV_LINE_MAX 2500
void fabLoop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < PROV_LINE_MAX) line += c; else tooLong = true;
      continue;
    }
    if (tooLong) Serial.println("PROV-ERR za dluga linia");
    else handle();
    line = "";
    tooLong = false;
  }
}
