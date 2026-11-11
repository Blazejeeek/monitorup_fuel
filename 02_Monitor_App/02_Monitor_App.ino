/*
  MonitorUp - monitoring poziomu zbiornika ppoz. i zbiornika paliwa
  Plytka: KinCony B8M (ESP32-S3-WROOM-1U N16R8)
  Plan: PLAN.md, opis: README.md

  Arduino: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 16MB, PSRAM OPI,
  partycje z pliku partitions.csv (2 x 3 MB OTA + LittleFS).
  Wgrywanie: ..\upload.ps1 -Sketch 02_Monitor_App -Compile   (albo przez strone: Ustawienia -> System -> OTA)
*/
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "hw.h"
#include "config.h"
#include "cfgjson.h"
#include "sensors.h"
#include "logger.h"
#include "timesync.h"
#include "stats.h"
#include "outputs.h"
#include "net.h"
#include "display.h"
#include "modbus.h"
#include "mqtt.h"
#include "summary.h"
#include "web.h"

#define BOOT_LOGO_MS 5000
#define WDT_TIMEOUT_MS 30000

static bool softReset() {
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_SW || r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static const char *resetName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "włączenie zasilania";
    case ESP_RST_SW: return "restart programowy";
    case ESP_RST_PANIC: return "błąd programu";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "spadek napięcia";
    case ESP_RST_USB: return "USB";
    default: return "inny";
  }
}

// Logo 5 s, w tym czasie test wyjsc OUT1..OUT6 po kolei
static void bootSequence() {
  bool test = cfg.bootTestEn && !(cfg.bootTestSkipSw && softReset());
  Serial.printf("Test wyjsc przy starcie: %s\n", test ? "TAK" : "pominiety");
  uint32_t t0 = millis();
  uint8_t phase = 0;
  if (test) {
    for (uint8_t k = 0; k < 8; k++) {
      if (!(cfg.bootTestMask & (1 << k))) continue;
      setOutputs(1 << k);
      char b[24]; snprintf(b, sizeof(b), "Test wyjsc: OUT%u", k + 1);
      uint32_t ts = millis();
      while (millis() - ts < cfg.bootTestMs) { oledBoot(b, phase++); delay(80); }
    }
    setOutputs(0);
  }
  while (millis() - t0 < BOOT_LOGO_MS) { oledBoot("", phase++); delay(80); }
}

// Przycisk DOWNLOAD (GPIO0) trzymany 10 s = reset loginu/hasla i sieci (kalibracja zostaje)
static void resetButtonLoop() {
  static uint32_t pressT = 0;
  if (digitalRead(PIN_BTN_DOWNLOAD) == LOW) {
    if (!pressT) pressT = millis();
    oledResetHoldMs = millis() - pressT;
    if (oledResetHoldMs >= 10000) {
      accountDefaults();
      netDefaults();
      cfgSave();
      logEvent("SYSTEM", "Reset loginu, hasła i sieci przyciskiem DOWNLOAD");
      delay(300);
      ESP.restart();
    }
  } else { pressT = 0; oledResetHoldMs = 0; }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== MonitorUp v" FW_VERSION " ===");

  pinMode(PIN_BTN_DOWNLOAD, INPUT_PULLUP);
  pinMode(SD_CD, INPUT_PULLUP);

  hwInitI2C();
  setOutputs(0);                 // wyjscia wylaczone jak najszybciej
  Serial.printf("PCF8575: %s\n", pcfOk ? "OK" : "BLAD");

  cfgLoad();
  Serial.printf("Ustawienia: %s\n", cfgSource.c_str());
  timeFromRtc();

  oledBegin();
  Serial.printf("OLED: %s\n", oledOk ? "OK" : "BLAD");
  oledBoot("", 0);

  loggerBegin();
  netBegin();
  sensorsBegin();
  bootSequence();
  outputsBegin();
  statsBegin();
  histPrefill();
  timeApply();
  mbApply();
  webBegin();
  mqttBegin();

  esp_task_wdt_config_t wc = {.timeout_ms = WDT_TIMEOUT_MS, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wc) != ESP_OK) esp_task_wdt_init(&wc);
  esp_task_wdt_add(NULL);

  logEvent("SYSTEM", String("Start v" FW_VERSION " (") + resetName() + ")");
  Serial.printf("Strona WWW: http://%s.local, AP http://192.168.4.1 (%s)\n", MDNS_NAME, cfg.apSsid);
}

void loop() {
  esp_task_wdt_reset();
  webLoop();
  mbLoop();
  mqttLoop();

  uint32_t now = millis();
  static uint32_t tDi = 0, tAl = 0, tOled = 0, tSec = 0, tLog = 0;
  static bool rotated = false;
  if (now - tDi >= 50)    { tDi = now; pcfRead(); }
  if (now - tAl >= 100)   { tAl = now; alarmLoop(); }
  if (now - tOled >= 500) { tOled = now; oledLoop(); }
  if (now - tSec >= 1000) {
    tSec = now;
    netLoop();
    sdLoop();
    summaryLoop();
    timeLoop();
    resetButtonLoop();
    if (histTick()) {
      uint32_t m = histLastClosed();
      statsMinute(m);
      if (cfg.logIntervalMin && ((m + 1) % cfg.logIntervalMin) == 0) logRow();
      struct tm tm; time_t t = time(nullptr); localtime_r(&t, &tm);
      if ((tm.tm_hour == 0 && tm.tm_min == 5) || !rotated) { rotated = true; logRotate(); }
    }
  }
  if (now - tLog >= 60000) {
    tLog = now;
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      TankState s = tankGet(i);
      Serial.printf("Z%u %s: U=%.3fV I=%.2fmA poziom=%.1fcm (%.0f%%) stan=%s\n", i + 1, cfg.tankName[i], s.volt, s.mA, s.cm, s.pct, stateName(tankEffState(i)));
    }
  }
}
