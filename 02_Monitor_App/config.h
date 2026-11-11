// MonitorUp - ustawienia: NVS (Preferences) + kopia kalibracji i progow w EEPROM 24C02 (CRC)
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_mac.h>
#include "hw.h"

#define TANK_COUNT 2
#define CFG_MAGIC   0x4D41u      // "MA"
#define CFG_VERSION 3

// Kalibracja i progi jednego zbiornika (ta czesc ma kopie w EEPROM)
struct __attribute__((packed)) TankCal {
  uint8_t enabled;
  uint8_t channel;        // 0..3 = zacisk A1..A4
  uint8_t type;           // 0 = 4-20 mA, 1 = 0-5 V
  uint8_t smooth;         // dodatkowe wygladzanie 0-100 %
  float   shuntOhm;       // rezystor pomiarowy wejscia 4-20 mA (do przeliczenia V -> mA)
  float   minX, minCm;    // punkt MIN: wartosc (mA lub V) -> poziom cm
  float   maxX, maxCm;    // punkt MAX
  float   offsetCm;       // przesuniecie montazowe sondy
  float   heightCm;       // wysokosc zbiornika (100 %)
  float   thrCrit;        // prog KRYTYCZNY (cm)
  float   thrMin;         // prog MINIMALNY (cm)
  float   thrHigh;        // prog ZA DUZY (cm)
  float   hyst;           // histereza (cm)
  uint16_t delayS;        // opoznienie alarmu (s)
};

struct NetIp { uint8_t dhcp; uint32_t ip, mask, gw, dns1, dns2; };

#define VOL_TABLE_N 20
enum TankShape : uint8_t { SH_HCYL = 0, SH_VCYL = 1, SH_BOX = 2, SH_TABLE = 3 };

// Litry i statystyki zbiornika
struct TankExtra {
  uint8_t  volEn;           // przeliczanie na litry
  uint8_t  shape;           // TankShape
  float    dimA, dimB, dimC; // HCYL: srednica, dlugosc | VCYL: srednica | BOX: dlugosc, szerokosc (cm)
  uint8_t  tblN;
  float    tblCm[VOL_TABLE_N], tblL[VOL_TABLE_N];
  uint8_t  statsEn;
  float    refillCm;        // wzrost uznawany za tankowanie (w 15 min)
  uint8_t  fcRef;           // prognoza do: 0 = MINIMALNY, 1 = KRYTYCZNY, 2 = dno
  uint8_t  fcWarnDays;      // ostrzezenie gdy prognoza < N dni (0 = wyl.)
  uint8_t  dropEn;          // alarm naglego spadku
  float    dropCm;          // Y cm ...
  uint16_t dropMin;         // ... w T min
  uint8_t  nightEn;
  uint8_t  nightFrom, nightTo; // godziny okna nocnego
  float    nightCm;
};

// Funkcje wyjsc
// Usterka zbiorcza: przyczyny (bity w cfg.faultMask i w faultBits())
enum FaultBit : uint8_t { FAULT_SENSOR = 1, FAULT_ADS = 2, FAULT_SD = 4, FAULT_TIME = 8, FAULT_NET = 16, FAULT_MQTT = 32, FAULT_PCF = 64 };
#define FAULT_DEFAULT (FAULT_SENSOR | FAULT_ADS | FAULT_SD)

enum OutFunc : uint8_t { OF_OFF = 0, OF_MANUAL, OF_SIREN, OF_LAMP, OF_COMMON, OF_TANK1, OF_TANK2, OF_WARN,
                         OF_MIN, OF_MAX, OF_CRIT, OF_FAULT };   // nowe funkcje dopisywane na koncu (zgodnosc ustawien)
struct OutCfg {
  uint8_t  func;
  uint8_t  blink;           // 0 = ciagle, 1 = przerywane
  uint16_t onMs, offMs;
  uint8_t  pwmPre;          // jasnosc przed skasowaniem 0-100 %
  uint8_t  pwmPost;         // jasnosc po skasowaniu 0-100 % (0 = wylacz)
  uint8_t  manual;          // wartosc dla trybu recznego 0-100 %
};

// Token API (przechowywany tylko skrot)
struct ApiToken {
  uint8_t  used;
  char     name[24];
  uint8_t  hash[32];
  uint8_t  perm;            // 0 = odczyt, 1 = odczyt + sterowanie, 2 = konfiguracja
  uint32_t created, expires; // epoch (expires 0 = bez terminu)
  uint32_t allowIp;         // 0 = dowolny
  uint32_t lastUsed, lastIp;
};

struct Config {
  uint16_t magic;
  uint16_t version;
  // konto
  char     user[24];
  uint8_t  salt[16];
  uint8_t  hash[32];
  uint8_t  mustChange;
  uint16_t sessionMin;
  // urzadzenie
  char     devName[24];
  // Access Point: 0 = zawsze, 1 = tylko awaryjnie, 2 = wylaczony
  uint8_t  apMode;
  char     apSsid[33];
  char     apPass[65];
  uint8_t  apChannel;
  // WiFi klient
  uint8_t  wifiEn;
  char     wifiSsid[33];
  char     wifiPass[65];
  NetIp    wifiIp;
  // Ethernet
  uint8_t  ethEn;
  NetIp    ethIp;
  // zbiorniki
  char     tankName[TANK_COUNT][24];
  TankCal  tank[TANK_COUNT];
  // test wyjsc przy starcie
  uint8_t  bootTestEn;
  uint8_t  bootTestMask;     // bity OUT1..OUT8
  uint16_t bootTestMs;
  uint8_t  bootTestSkipSw;   // pomijaj po restarcie programowym / watchdogu

  // ===== od wersji 2 (pola dopisywane WYLACZNIE na koncu - stare dane zostaja wczytane jako prefiks) =====
  TankExtra tx[TANK_COUNT];  // litry i statystyki
  OutCfg   out[8];
  uint8_t  lampHwGpio;       // 0 = brak, 13/14/21 = sprzetowy PWM lampki na wolnym GPIO (przez modul MOSFET)
  uint8_t  lampOnMin;        // lampka przy MINIMALNYM: 0 = wyl., 1 = slabe mruganie
  uint16_t reminderMin;      // ponowna syrena po X min trwania skasowanego alarmu (0 = wyl.)
  uint8_t  ackDi1En;         // kasowanie alarmu wejsciem DI1
  // czas
  uint8_t  ntpEn;
  char     ntpServer[48];
  char     ntpServer2[48];
  uint16_t ntpIntervalMin;
  // logi
  uint16_t logIntervalMin;
  uint16_t logRetentionDays;
  // MQTT
  uint8_t  mqttEn;
  char     mqttHost[64];
  uint16_t mqttPort;
  char     mqttUser[32];
  char     mqttPass[64];
  char     mqttPrefix[48];
  uint16_t mqttIntervalS;
  uint8_t  mqttHa;
  uint8_t  mqttCmdEn;
  // Modbus TCP
  uint8_t  mbEn;
  uint16_t mbPort;
  uint8_t  mbUnit;
  uint8_t  mbWriteEn;
  uint32_t mbAllowIp[4];
  // API
  ApiToken tok[8];
  uint8_t  apiLocalRead;
  // OLED
  uint16_t oledDimMin;
  // Usterka zbiorcza (wyjscie OF_FAULT): co ja wlacza, bity FAULT_* (pole dopisane w 0.4.2)
  uint8_t  faultMask;
  // Kasowanie alarmu: 0 = reczne (alarm trwa do skasowania), 1 = automatyczne (bez kasowania, gasnie po powrocie do normy) (0.4.4)
  uint8_t  ackAuto;
};

extern Config cfg;
extern String cfgSource;         // skad wczytano ustawienia

void pwSet(const char *pw);
bool pwCheck(const char *pw);
void tankDefaults(uint8_t i);
void tankExtraDefaults(uint8_t i);
void netDefaults();
void accountDefaults();
void outDefaults();
void cfgDefaults();
bool eeBackupSave();
bool eeBackupLoad(TankCal *out);
void cfgSave();
void cfgLoad();
