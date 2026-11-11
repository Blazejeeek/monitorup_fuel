// MonitorUp - warstwa sprzetowa KinCony B8M: piny, muteks I2C, PCF8575, ADS1115, 24C02, DS3231
#include "hw.h"
#include <Arduino.h>
#include <Wire.h>


// ---------------- Piny (wg dokumentacji KinCony B8M) ----------------





// ---------------- Muteks I2C ----------------
// Wszystkie uklady siedza na jednej magistrali, a pomiar dziala w osobnym zadaniu.
SemaphoreHandle_t i2cMux = nullptr;


void hwInitI2C() {
  if (!i2cMux) i2cMux = xSemaphoreCreateRecursiveMutex();
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  Wire.setTimeOut(50);
}

bool i2cPresent(uint8_t addr) {
  I2CLock l;
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// =====================================================================
//  PCF8575: P0-P7 wyjscia MOSFET (logika odwrocona: 0 = ON), P8-P15 wejscia DI
// =====================================================================
uint8_t outState = 0;    // bit = 1 -> wyjscie wlaczone
uint8_t diState  = 0;    // bit = 1 -> wejscie aktywne (styk zwarty)
bool pcfOk = false;

bool pcfWrite() {
  I2CLock l;
  Wire.beginTransmission(ADDR_PCF8575);
  Wire.write((uint8_t)~outState);
  Wire.write(0xFF);               // P8-P15 jako wejscia (quasi-input)
  pcfOk = (Wire.endTransmission() == 0);
  return pcfOk;
}

void setOutputs(uint8_t mask) { outState = mask; pcfWrite(); }

bool pcfRead() {
  I2CLock l;
  if (Wire.requestFrom((uint8_t)ADDR_PCF8575, (uint8_t)2) != 2) { pcfOk = false; return false; }
  Wire.read();
  uint8_t hi = Wire.read();
  diState = ~hi;
  pcfOk = true;
  return true;
}

// =====================================================================
//  ADS1115: pojedynczy pomiar, PGA +-6,144 V, 128 SPS
// =====================================================================

int16_t adsReadChannel(uint8_t ch, bool &ok) {
  uint16_t cfg = 0x8000 | ((uint16_t)(4 + ch) << 12) | (0 << 9) | 0x0100 | (4 << 5) | 0x0003;
  {
    I2CLock l;
    Wire.beginTransmission(ADDR_ADS1115);
    Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
    if (Wire.endTransmission() != 0) { ok = false; return 0; }
  }
  delay(9);                         // konwersja 128 SPS ~7,8 ms (magistrala wolna w tym czasie)
  I2CLock l;
  Wire.beginTransmission(ADDR_ADS1115);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) { ok = false; return 0; }
  if (Wire.requestFrom((uint8_t)ADDR_ADS1115, (uint8_t)2) != 2) { ok = false; return 0; }
  int16_t v = (Wire.read() << 8) | Wire.read();
  ok = true;
  return v;
}

// =====================================================================
//  EEPROM 24C02 (256 B, strony po 8 B)
// =====================================================================
bool eeReadBlock(uint8_t addr, uint8_t *buf, uint16_t len) {
  I2CLock l;
  while (len) {
    uint8_t n = len > 32 ? 32 : len;
    Wire.beginTransmission(ADDR_EEPROM);
    Wire.write(addr);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ADDR_EEPROM, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    addr += n; buf += n; len -= n;
  }
  return true;
}

// Zapis tylko stron, ktore sie zmienily (oszczedza EEPROM)
bool eeWriteBlock(uint8_t addr, const uint8_t *data, uint16_t len) {
  while (len) {
    uint8_t n = 8 - (addr % 8);
    if (n > len) n = len;
    uint8_t cur[8];
    if (!eeReadBlock(addr, cur, n)) return false;
    if (memcmp(cur, data, n) != 0) {
      {
        I2CLock l;
        Wire.beginTransmission(ADDR_EEPROM);
        Wire.write(addr);
        Wire.write(data, n);
        if (Wire.endTransmission() != 0) return false;
      }
      bool done = false;
      for (uint8_t i = 0; i < 20 && !done; i++) { delay(1); done = i2cPresent(ADDR_EEPROM); }
      if (!done) return false;
    }
    addr += n; data += n; len -= n;
  }
  return true;
}

// =====================================================================
//  DS3231 - przechowuje czas UTC
// =====================================================================
static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

RtcInfo rtcInfo = {};

// Zwraca czas UTC (epoch) albo 0 przy bledzie
time_t rtcReadEpoch() {
  I2CLock l;
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)7) != 7) { rtcInfo.ok = false; return 0; }
  struct tm t = {};
  t.tm_sec  = bcd2dec(Wire.read() & 0x7F);
  t.tm_min  = bcd2dec(Wire.read() & 0x7F);
  t.tm_hour = bcd2dec(Wire.read() & 0x3F);
  Wire.read();
  t.tm_mday = bcd2dec(Wire.read() & 0x3F);
  t.tm_mon  = bcd2dec(Wire.read() & 0x1F) - 1;
  t.tm_year = 100 + bcd2dec(Wire.read());

  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)1) == 1)
    rtcInfo.osf = Wire.read() & 0x80;
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x11);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)2) == 2) {
    int8_t msb = (int8_t)Wire.read();
    uint8_t lsb = Wire.read();
    rtcInfo.temp = msb + (lsb >> 6) * 0.25f;
  }
  rtcInfo.ok = true;
  // timegm: konwersja bez strefy czasowej
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  if (m <= 2) { y--; m += 12; }
  long days = 365L * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + t.tm_mday - 719469L;
  return (time_t)days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
}

bool rtcWriteEpoch(time_t utc) {
  struct tm t;
  gmtime_r(&utc, &t);
  I2CLock l;
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x00);
  Wire.write(dec2bcd(t.tm_sec)); Wire.write(dec2bcd(t.tm_min)); Wire.write(dec2bcd(t.tm_hour));
  Wire.write(t.tm_wday + 1);
  Wire.write(dec2bcd(t.tm_mday)); Wire.write(dec2bcd(t.tm_mon + 1)); Wire.write(dec2bcd(t.tm_year % 100));
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)1) != 1) return false;
  uint8_t st = Wire.read() & 0x7F;  // skasuj flage OSF
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F); Wire.write(st);
  bool ok = Wire.endTransmission() == 0;
  if (ok) rtcInfo.osf = false;
  return ok;
}
