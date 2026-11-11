// Monitor App - pomiar przetwornikow: probki co 100 ms, srednia z okna 2 s, litry, stany alarmowe
#pragma once
#include <Arduino.h>
#include "config.h"

enum TankStateCode : uint8_t { ST_NORMAL = 0, ST_MIN = 1, ST_CRIT = 2, ST_HIGH = 3, ST_FAULT = 4, ST_OFF = 5, ST_DROP = 6 };

struct TankState {
  bool    valid;        // pomiar wazny (czujnik sprawny, kanal wlaczony)
  int16_t raw;          // ostatnia surowa wartosc ADS1115
  float   volt;         // napiecie po usrednieniu
  float   mA;           // prad (dla 4-20 mA)
  float   cm;           // poziom
  float   pct;          // wypelnienie 0-100 % (z objetosci, gdy wlaczone litry)
  float   vol;          // litry (gdy wlaczone przeliczanie, inaczej -1)
  float   volTotal;     // pojemnosc calkowita
  uint8_t state;        // TankStateCode (stan wynikajacy z poziomu)
  uint32_t updated;     // millis() ostatniego wyniku
};

extern bool adsOk;

const char *stateName(uint8_t s);
TankState tankGet(uint8_t i);
float tankVolumeL(uint8_t i, float cm);
float tankTotalL(uint8_t i);
void sensorsReset();     // po zmianie wejscia/kalibracji
void sensorsBegin();
