// MonitorUp - tabela kalibracji pradu wejscia A4: surowy odczyt ADS1115 -> mA rzeczywiste (od 0.4.8)
//
// Zapis niezalezny od struktury ustawien (zmiana firmware'u jej nie kasuje):
//  - NVS, przestrzen "monitor", klucz "mAcal" (pelna tabela z opisem),
//  - kopia w EEPROM 24C02 od adresu 128 (punkty jako uint16, CRC16) - z niej tabela wraca, gdy NVS zostal wyczyszczony.
// Bez tabeli prad = napiecie / 150 ohm (rezystor pomiarowy staly).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "hw.h"                 // ADS_FS_V

#define CAL_MAX       24          // maks. punktow tabeli
#define SHUNT_OHM     150.0f      // rezystor pomiarowy wejscia 4-20 mA (staly)

struct CalTable {
  uint16_t magic;                 // 'CT'
  uint8_t  ver;
  uint8_t  n;                     // 0 = brak tabeli
  char     date[20];              // data pomiaru z pliku
  char     ref[40];               // wzorzec / opis zrodla
  float    raw[CAL_MAX];          // odpowiednik surowego ADC (rosnaco)
  float    mA[CAL_MAX];           // prad rzeczywisty
};

extern CalTable calTab;
extern String calSource;          // skad wczytano tabele (NVS / EEPROM / brak)

void   calLoad();                                  // przy starcie: NVS, a gdy brak - EEPROM (i odtworzenie NVS)
float  calRawToMa(float raw);                      // mA z tabeli albo napiecie / 150 ohm
String calFromJson(JsonObjectConst f);             // import pliku kalibracji; "" = OK, inaczej opis bledu
void   calToJson(JsonObject o);                    // eksport w formacie pliku
void   calClear();
