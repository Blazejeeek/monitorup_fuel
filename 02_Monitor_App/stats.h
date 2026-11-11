// MonitorUp - statystyki zuzycia, tankowania, prognoza, alarm naglego spadku
// Zuzycie doby = poziom o 00:00 - poziom teraz + suma tankowan (odporne na szum pomiaru).
#pragma once
#include <Arduino.h>
#include "config.h"

#define STAT_DAYS 30

struct DayRec { uint32_t day; float consCm, consL, refCm, refL; uint8_t full; };   // day = YYYYMMDD

struct TankStats {
  // biezaca doba
  uint32_t day;                 // YYYYMMDD
  float    level00;             // poziom (cm) na poczatku doby, NAN = nieznany
  uint8_t  full;                // urzadzenie pracowalo od polnocy
  float    refCm, refL;         // suma tankowan dzis
  // tankowanie w toku
  bool     refilling;
  float    refBase, refPeak;
  uint32_t refPeakMin;
  // ostatnie tankowanie
  float    lastRefCm, lastRefL;
  uint32_t lastRefT;
  // historia dzienna (najnowsza na koncu)
  DayRec   days[STAT_DAYS];
  uint8_t  nDays;
  // nagly spadek
  bool     drop;
  float    dropAmount;
  uint32_t dropT, dropLastMin;
  uint32_t suspendUntil;        // epoch: wstrzymanie alarmu naglego spadku
};

extern TankStats st[TANK_COUNT];

float statConsToday(uint8_t i, bool litres);
float statAvg(uint8_t i, uint8_t n, bool litres);
float statYesterday(uint8_t i, bool litres);
float statForecast(uint8_t i);     // dni, NAN = brak danych
bool statForecastWarn(uint8_t i);
void statsBegin();
void statsMinute(uint32_t m);
void statsDropClear(bool acked);
void statsSuspend(uint8_t i, uint16_t minutes);
