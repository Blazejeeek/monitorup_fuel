// Monitor App - warstwa sprzetowa KinCony B8M: piny, muteks I2C, PCF8575, ADS1115, 24C02, DS3231
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <freertos/semphr.h>

#define FW_VERSION "0.3.0"

// ---------------- Piny (wg dokumentacji KinCony B8M) ----------------
#define I2C_SDA 8
#define I2C_SCL 18
#define ADDR_PCF8575 0x22
#define ADDR_ADS1115 0x48
#define ADDR_DS3231  0x68
#define ADDR_OLED    0x3C
#define ADDR_EEPROM  0x50
#define ETH_CS   42
#define ETH_INT  43
#define ETH_RST  44
#define ETH_SCK  1
#define ETH_MISO 41
#define ETH_MOSI 2
#define SD_MOSI 10
#define SD_SCK  11
#define SD_MISO 12
#define SD_CS   9
#define SD_CD   5
#define PIN_BTN_DOWNLOAD 0
#define ADS_FS_V 6.144f

// Muteks I2C: wszystkie uklady na jednej magistrali, pomiar i PWM w osobnych zadaniach
extern SemaphoreHandle_t i2cMux;
struct I2CLock {
  bool ok;
  I2CLock(uint32_t ms = 200) { ok = xSemaphoreTakeRecursive(i2cMux, pdMS_TO_TICKS(ms)) == pdTRUE; }
  ~I2CLock() { if (ok) xSemaphoreGiveRecursive(i2cMux); }
};

extern uint8_t outState;     // bit = 1 -> wyjscie wlaczone
extern uint8_t diState;      // bit = 1 -> wejscie aktywne (styk zwarty)
extern bool pcfOk;

struct RtcInfo { bool ok; bool osf; float temp; };
extern RtcInfo rtcInfo;

void hwInitI2C();
bool i2cPresent(uint8_t addr);
bool pcfWrite();
void setOutputs(uint8_t mask);
bool pcfRead();
int16_t adsReadChannel(uint8_t ch, bool &ok);
bool eeReadBlock(uint8_t addr, uint8_t *buf, uint16_t len);
bool eeWriteBlock(uint8_t addr, const uint8_t *data, uint16_t len);
time_t rtcReadEpoch();           // UTC, 0 = blad
bool rtcWriteEpoch(time_t utc);
