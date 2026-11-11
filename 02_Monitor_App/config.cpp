// MonitorUp - ustawienia: NVS (Preferences) + kopia kalibracji i progow w EEPROM 24C02 (CRC)
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_mac.h>
#include "hw.h"

Config cfg;
Preferences prefs;

// ---------------- Haslo: SHA-256(sol + haslo) ----------------
void pwHash(const uint8_t salt[16], const char *pw, uint8_t out[32]) {
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, salt, 16);
  mbedtls_sha256_update(&c, (const uint8_t *)pw, strlen(pw));
  mbedtls_sha256_finish(&c, out);
  mbedtls_sha256_free(&c);
}

void pwSet(const char *pw) {
  esp_fill_random(cfg.salt, sizeof(cfg.salt));
  pwHash(cfg.salt, pw, cfg.hash);
}

bool pwCheck(const char *pw) {
  uint8_t h[32];
  pwHash(cfg.salt, pw, h);
  uint8_t d = 0;
  for (int i = 0; i < 32; i++) d |= h[i] ^ cfg.hash[i];   // porownanie w stalym czasie
  return d == 0;
}

// ---------------- Wartosci domyslne ----------------
void tankDefaults(uint8_t i) {
  TankCal &t = cfg.tank[i];
  t.enabled = 1;
  t.channel = i == 0 ? 3 : 2;     // zbiornik paliwa = A4, opcjonalny drugi = A3
  t.type = 0;
  t.smooth = 0;
  t.shuntOhm = 150.0f;            // do weryfikacji przy kalibracji (wartosc bocznika B8M nieznana)
  t.minX = 4.0f;  t.minCm = 0.0f;
  t.maxX = 20.0f; t.maxCm = 300.0f;
  t.offsetCm = 0.0f;
  t.heightCm = 300.0f;
  t.thrCrit = 30.0f;
  t.thrMin = 60.0f;
  t.thrHigh = 280.0f;
  t.hyst = 2.0f;
  t.delayS = 10;
  t.enabled = i == 0;               // domyslnie jeden zbiornik (paliwo, A4)
  strlcpy(cfg.tankName[i], i == 0 ? "Zbiornik paliwa" : "Zbiornik 2", sizeof(cfg.tankName[i]));
}

void netDefaults() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  cfg.apMode = 0;
  snprintf(cfg.apSsid, sizeof(cfg.apSsid), "MonitorUp.pl-%02X%02X", mac[4], mac[5]);
  strlcpy(cfg.apPass, "monitor123", sizeof(cfg.apPass));
  cfg.apChannel = 6;
  cfg.wifiEn = 0;
  cfg.wifiSsid[0] = 0; cfg.wifiPass[0] = 0;
  cfg.wifiIp = {1, 0, 0, 0, 0, 0};
  cfg.ethEn = 1;
  cfg.ethIp = {1, 0, 0, 0, 0, 0};
}

void accountDefaults() {
  strlcpy(cfg.user, "admin", sizeof(cfg.user));
  pwSet("admin");
  cfg.mustChange = 1;
  cfg.sessionMin = 30;
}

void outDefaults() {
  memset(cfg.out, 0, sizeof(cfg.out));
  for (auto &o : cfg.out) { o.onMs = 500; o.offMs = 500; o.pwmPre = 100; o.pwmPost = 0; }
  cfg.out[0].func = OF_MIN;   cfg.out[0].pwmPost = 100;            // OUT1 poziom MINIMALNY
  cfg.out[1].func = OF_MAX;   cfg.out[1].pwmPost = 100;            // OUT2 poziom ZA DUZY (maksymalny)
  cfg.out[2].func = OF_CRIT;  cfg.out[2].pwmPost = 100;            // OUT3 poziom KRYTYCZNY
  cfg.out[3].func = OF_FAULT; cfg.out[3].pwmPost = 100;            // OUT4 usterka zbiorcza (przekaznik)
  cfg.out[4].func = OF_SIREN;                                      // OUT5 buzzer – milknie po skasowaniu
  cfg.faultMask = FAULT_DEFAULT;
  cfg.ackAuto = 0;
  cfg.faultBlink = 1;
}

void tankExtraDefaults(uint8_t i) {
  TankExtra &x = cfg.tx[i];
  memset(&x, 0, sizeof(x));
  x.volEn = 0;
  x.shape = SH_HCYL;             // zbiornik paliwa: walec lezacy
  x.dimA = 200; x.dimB = 300; x.dimC = 0;
  x.statsEn = 1;                 // statystyki zuzycia zawsze wlaczone (0.4.4)
  x.refillCm = 2.0f;
  x.fcRef = 0;
  x.fcWarnDays = 3;
  x.dropEn = 1;
  x.dropCm = 3.0f; x.dropMin = 10;
  x.nightEn = i == 0;
  x.nightFrom = 22; x.nightTo = 6; x.nightCm = 1.0f;
}

void cfgDefaultsV2() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) tankExtraDefaults(i);
  outDefaults();
  cfg.lampHwGpio = 0;
  cfg.lampOnMin = 0;
  cfg.reminderMin = 0;
  cfg.ackDi1En = 1;
  cfg.ntpEn = 1;
  strlcpy(cfg.ntpServer, "pool.ntp.org", sizeof(cfg.ntpServer));
  strlcpy(cfg.ntpServer2, "time.google.com", sizeof(cfg.ntpServer2));
  cfg.ntpIntervalMin = 60;
  cfg.logIntervalMin = 5;
  cfg.logRetentionDays = 3650;      // 10 lat (0.4.5)
  cfg.mqttEn = 0; cfg.mqttHost[0] = 0; cfg.mqttPort = 1883;
  cfg.mqttUser[0] = 0; cfg.mqttPass[0] = 0;
  strlcpy(cfg.mqttPrefix, "monitorup/monitor", sizeof(cfg.mqttPrefix));
  cfg.mqttIntervalS = 10; cfg.mqttHa = 1; cfg.mqttCmdEn = 1;
  cfg.mbEn = 0; cfg.mbPort = 502; cfg.mbUnit = 1; cfg.mbWriteEn = 0;
  memset(cfg.mbAllowIp, 0, sizeof(cfg.mbAllowIp));
  memset(cfg.tok, 0, sizeof(cfg.tok));
  cfg.apiLocalRead = 0;
  cfg.oledDimMin = 10;
}

// Wersja 3: urzadzenie dla jednego zbiornika paliwa. Slot 0 = paliwo na A4 (przejmuje kalibracje z A4), slot 1 = opcjonalny, wylaczony.
void migrateSingleTank() {
  TankCal a3 = cfg.tank[0];
  cfg.tank[0] = cfg.tank[1];             // kalibracja i progi wykonane na A4
  cfg.tank[0].channel = 3;
  cfg.tank[0].enabled = 1;
  cfg.tank[1] = a3;
  cfg.tank[1].channel = 2;
  strlcpy(cfg.tankName[0], "Zbiornik paliwa", sizeof(cfg.tankName[0]));
  cfg.tx[0] = cfg.tx[1];                 // ustawienia paliwa: walec lezacy, statystyki, okno nocne
  cfg.tx[0].statsEn = 1;
  cfg.tx[0].nightEn = 1;
  cfg.tank[1].enabled = 0;
  strlcpy(cfg.tankName[1], "Zbiornik 2", sizeof(cfg.tankName[1]));
  tankExtraDefaults(1);
  cfg.tx[1].statsEn = 1; cfg.tx[1].nightEn = 0;
}

void cfgDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  accountDefaults();
  strlcpy(cfg.devName, "MonitorUp.pl", sizeof(cfg.devName));
  netDefaults();
  for (uint8_t i = 0; i < TANK_COUNT; i++) tankDefaults(i);
  cfg.bootTestEn = 1;
  cfg.bootTestMask = 0x3F;         // OUT1..OUT6
  cfg.bootTestMs = 800;
  cfg.bootTestSkipSw = 1;
  cfgDefaultsV2();
}

// ---------------- Kopia w EEPROM 24C02 ----------------
// [0..1] 'M''A', [2] wersja, [3] dlugosc, [4..] dane, potem CRC16
#define EE_BASE 0
static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}

static_assert(sizeof(TankCal) * TANK_COUNT + 6 <= 256, "kopia nie miesci sie w 24C02");

bool eeBackupSave() {
  uint8_t buf[sizeof(TankCal) * TANK_COUNT + 6];
  buf[0] = 'M'; buf[1] = 'A'; buf[2] = CFG_VERSION; buf[3] = sizeof(TankCal) * TANK_COUNT;
  memcpy(buf + 4, cfg.tank, sizeof(TankCal) * TANK_COUNT);
  uint16_t c = crc16(buf, sizeof(buf) - 2);
  buf[sizeof(buf) - 2] = c & 0xFF; buf[sizeof(buf) - 1] = c >> 8;
  return eeWriteBlock(EE_BASE, buf, sizeof(buf));
}

bool eeBackupLoad(TankCal *out) {
  uint8_t buf[sizeof(TankCal) * TANK_COUNT + 6];
  if (!eeReadBlock(EE_BASE, buf, sizeof(buf))) return false;
  if (buf[0] != 'M' || buf[1] != 'A' || buf[3] != sizeof(TankCal) * TANK_COUNT) return false;
  uint16_t c = crc16(buf, sizeof(buf) - 2);
  if ((buf[sizeof(buf) - 2] | (buf[sizeof(buf) - 1] << 8)) != c) return false;
  memcpy(out, buf + 4, sizeof(TankCal) * TANK_COUNT);
  return true;
}

// ---------------- NVS ----------------
String cfgSource;   // skad wczytano ustawienia (do logu/strony)

void cfgSave() {
  prefs.begin("monitor", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
  if (!eeBackupSave()) Serial.println("EEPROM: blad zapisu kopii");
}

void cfgLoad() {
  bool ok = false;
  prefs.begin("monitor", true);
  size_t len = prefs.getBytesLength("cfg");
  // Starsza wersja (krotszy blok) = prefiks obecnej struktury: wczytaj na wartosci domyslne nowych pol
  if (len >= offsetof(Config, tx) && len <= sizeof(cfg)) {
    cfgDefaults();
    prefs.getBytes("cfg", &cfg, len);
    ok = cfg.magic == CFG_MAGIC;
  }
  prefs.end();
  if (ok) {
    cfgSource = "NVS";
    if (cfg.version < 3) migrateSingleTank();
    // nazwa MonitorUp.pl: zamiana dawnych nazw domyslnych (nazwa urzadzenia, SSID AP); prefiks MQTT bez zmian
    bool brand = false;
    if (!strcmp(cfg.devName, "Monitor App") || !strcmp(cfg.devName, "MonitorUp")) { strlcpy(cfg.devName, "MonitorUp.pl", sizeof(cfg.devName)); brand = true; }
    for (const char *old : {"MonitorApp-", "MonitorUp-"}) {
      size_t n = strlen(old);
      if (!strncmp(cfg.apSsid, old, n)) { String sfx = cfg.apSsid + n; snprintf(cfg.apSsid, sizeof(cfg.apSsid), "MonitorUp.pl-%s", sfx.c_str()); brand = true; break; }
    }
    if (brand) { cfgSource += " (nazwa MonitorUp.pl)"; cfgSave(); }
    for (auto &x : cfg.tx) x.statsEn = 1;   // 0.4.4: bez przelacznika statystyk
    if (len != sizeof(cfg) || cfg.version != CFG_VERSION) {
      cfgSource += " (aktualizacja ustawien do wersji " + String(CFG_VERSION) + ")";
      cfg.version = CFG_VERSION;
      cfgSave();
    }
    return;
  }

  cfgDefaults();
  TankCal t[TANK_COUNT];
  if (eeBackupLoad(t)) {
    memcpy(cfg.tank, t, sizeof(t));
    cfgSource = "domyslne + kalibracja z EEPROM";
  } else cfgSource = "domyslne";
  cfgSave();
}
