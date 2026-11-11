// Monitor App - czas: DS3231 (UTC), NTP, ustawianie z przegladarki, strefa Polska
#pragma once
#include <Arduino.h>

#define TZ_POLAND "CET-1CEST,M3.5.0,M10.5.0/3"

extern String timeSource;
extern uint32_t lastNtpSync;

void timeFromRtc();
void timeApply();                  // po zmianie ustawien NTP
void timeSyncNow();
void timeSetManual(time_t utc, const char *src);
bool timeOk();
void timeLoop();
