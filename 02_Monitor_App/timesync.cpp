// MonitorUp - czas: DS3231 (UTC) jako zrodlo, NTP, ustawianie z przegladarki, strefa Polska
#include "timesync.h"
#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "hw.h"
#include "config.h"
#include "logger.h"

String timeSource = "brak";
uint32_t lastNtpSync = 0;           // epoch ostatniej synchronizacji NTP
static volatile bool ntpSyncedFlag = false;

static void ntpCb(struct timeval *tv) { ntpSyncedFlag = true; }

void timeFromRtc() {
  setenv("TZ", TZ_POLAND, 1);
  tzset();
  time_t t = rtcReadEpoch();
  if (t > 1700000000) {
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    timeSource = rtcInfo.osf ? "DS3231 (niepewny – flaga OSF)" : "DS3231";
  }
  Serial.printf("RTC: %s%s\n", rtcInfo.ok ? "OK" : "BLAD", rtcInfo.osf ? " (czas nieustawiony - flaga OSF)" : "");
}

void timeApply() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  if (!cfg.ntpEn) return;
  sntp_set_time_sync_notification_cb(ntpCb);
  sntp_set_sync_interval((uint32_t)cfg.ntpIntervalMin * 60000UL);
  configTzTime(TZ_POLAND, cfg.ntpServer, cfg.ntpServer2[0] ? cfg.ntpServer2 : nullptr);
}

void timeSyncNow() { if (cfg.ntpEn) { esp_sntp_restart(); } }

void timeSetManual(time_t utc, const char *src) {
  struct timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  bool ok = rtcWriteEpoch(utc);
  timeSource = src;
  logEvent("CZAS", String("Ustawiono czas (") + src + ")" + (ok ? "" : " – błąd zapisu DS3231"));
}

bool timeOk() { return timeValid() && (!rtcInfo.osf || lastNtpSync); }

void timeLoop() {      // co 1 s
  if (ntpSyncedFlag) {
    ntpSyncedFlag = false;
    bool first = !lastNtpSync;
    lastNtpSync = time(nullptr);
    rtcWriteEpoch(lastNtpSync);
    timeSource = "NTP (" + String(cfg.ntpServer) + ")";
    if (first) logEvent("CZAS", "Synchronizacja NTP: " + String(cfg.ntpServer));
  }
  static uint32_t tRtc = 0;
  if (millis() - tRtc > 3600000UL) {            // co godzine: bez NTP koryguj zegar ESP32 z DS3231
    tRtc = millis();
    if (!cfg.ntpEn || !lastNtpSync) {
      time_t t = rtcReadEpoch();
      if (t > 1700000000 && !rtcInfo.osf) { struct timeval tv = {t, 0}; settimeofday(&tv, nullptr); }
    }
  }
}
