/*
  KinCony B8M (ESP32-S3-WROOM-1U N16R8) - program testowy peryferiow
  Wersja 0.01

  - WiFi AP:  SSID "KinCony-B8M-Test", haslo "b8mtest123", IP 192.168.4.1
  - mDNS:     http://b8m-test.local
  - Ethernet: W5500, DHCP (strona WWW dostepna rowniez po kablu)
  - Strona WWW z testem: wyjsc MOSFET, wejsc DI, ADS1115, DS3231, 24C02,
    OLED SSD1306, karty SD, 1-Wire, wolnych GPIO, RS485, UART Tuya, I2C.

  Plytka Arduino: ESP32S3 Dev Module, USB CDC On Boot: Enabled,
  Flash 16MB, PSRAM: OPI, Partition: 16M Flash (3MB APP/9.9MB FATFS).
*/
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "oled.h"
#include "web_page.h"

#define FW_VERSION "0.01"

// ---------------- Konfiguracja sieci ----------------
static const char *AP_SSID   = "KinCony-B8M-Test";
static const char *AP_PASS   = "b8mtest123";      // min. 8 znakow
static const char *MDNS_NAME = "b8m-test";

// ---------------- Piny (wg dokumentacji KinCony) ----------------
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

#define RS485_RX 38
#define RS485_TX 39
#define TUYA_RX  17
#define TUYA_TX  16

#define PIN_BTN_DOWNLOAD 0

static const uint8_t OW_PINS[4]   = {47, 48, 7, 40};
static const uint8_t GPIO_FREE[3] = {13, 14, 21};

// ---------------- Obiekty ----------------
WebServer server(80);
MiniOLED oled;
SPIClass sdSPI(FSPI);
HardwareSerial RS485(1);
HardwareSerial TuyaSer(2);

OneWire ow0(OW_PINS[0]), ow1(OW_PINS[1]), ow2(OW_PINS[2]), ow3(OW_PINS[3]);
OneWire *owBus[4] = {&ow0, &ow1, &ow2, &ow3};
DallasTemperature dt0(&ow0), dt1(&ow1), dt2(&ow2), dt3(&ow3);
DallasTemperature *dtBus[4] = {&dt0, &dt1, &dt2, &dt3};

// ---------------- Stan ----------------
uint8_t outState = 0;          // bit = 1 -> wyjscie wlaczone
uint8_t diState  = 0;          // bit = 1 -> wejscie aktywne (zwarte)
bool pcfOk = false;
bool adsOk = false;
int16_t adcRaw[4] = {0};
float adcV[4] = {0};

bool chaseOn = false;
uint8_t chaseIdx = 0;
uint32_t chaseT = 0;

struct RtcData { bool ok; uint16_t y; uint8_t mo, d, h, mi, s; float temp; bool osf; } rtc = {};

bool sdMounted = false;
uint64_t sdSizeMB = 0;

uint8_t owCount[4] = {0};
float owTemp[4][4];            // do 4 czujnikow na magistrale
bool ethStarted = false;

struct FreeGpio { uint8_t pin; bool out; bool val; } gpioCfg[3];

uint8_t rsBuf[256];  uint16_t rsLen = 0;
uint8_t tuyaBuf[256]; uint16_t tuyaLen = 0;
uint32_t rsBaud = 9600;

String i2cList;
enum OledMode { OLED_STATUS, OLED_FILL, OLED_CLEAR } oledMode = OLED_STATUS;
bool oledInv = false;

struct SelfTest { const char *name; bool ok; String info; };
SelfTest selfTests[12];
uint8_t selfCount = 0;

// =====================================================================
//                          I2C - narzedzia
// =====================================================================
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void i2cScan() {
  i2cList = "";
  for (uint8_t a = 1; a < 127; a++) {
    if (i2cPresent(a)) {
      char b[8]; snprintf(b, sizeof(b), "0x%02X ", a);
      i2cList += b;
    }
  }
  i2cList.trim();
}

// ---------------- PCF8575 ----------------
// Wyjscia P0-P7 logika odwrocona (0 = ON). Wejscia P8-P15: wpisujemy 1 (quasi-input).
bool pcfWrite() {
  Wire.beginTransmission(ADDR_PCF8575);
  Wire.write((uint8_t)~outState);
  Wire.write(0xFF);
  pcfOk = (Wire.endTransmission() == 0);
  return pcfOk;
}

bool pcfRead() {
  if (Wire.requestFrom((uint8_t)ADDR_PCF8575, (uint8_t)2) != 2) { pcfOk = false; return false; }
  Wire.read();                       // P0-P7 (wyjscia)
  uint8_t hi = Wire.read();          // P8-P15 (wejscia)
  diState = ~hi;                     // zwarcie = 0 na pinie -> aktywne
  pcfOk = true;
  return true;
}

// ---------------- ADS1115 ----------------
int16_t adsReadChannel(uint8_t ch, bool &ok) {
  uint16_t cfg = 0x8000 | ((uint16_t)(4 + ch) << 12) | (0 << 9) /*PGA 6.144V*/ | 0x0100 /*single*/ |
                 (4 << 5) /*128 SPS*/ | 0x0003 /*comp off*/;
  Wire.beginTransmission(ADDR_ADS1115);
  Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
  if (Wire.endTransmission() != 0) { ok = false; return 0; }
  delay(9);
  Wire.beginTransmission(ADDR_ADS1115);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) { ok = false; return 0; }
  if (Wire.requestFrom((uint8_t)ADDR_ADS1115, (uint8_t)2) != 2) { ok = false; return 0; }
  int16_t v = (Wire.read() << 8) | Wire.read();
  ok = true;
  return v;
}

void adsReadAll() {
  bool ok = true;
  for (uint8_t i = 0; i < 4 && ok; i++) {
    adcRaw[i] = adsReadChannel(i, ok);
    adcV[i] = adcRaw[i] * 6.144f / 32768.0f;
  }
  adsOk = ok;
}

// ---------------- DS3231 ----------------
static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

void rtcRead() {
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)7) != 7) { rtc.ok = false; return; }
  rtc.s  = bcd2dec(Wire.read() & 0x7F);
  rtc.mi = bcd2dec(Wire.read() & 0x7F);
  rtc.h  = bcd2dec(Wire.read() & 0x3F);
  Wire.read();                                   // dzien tygodnia
  rtc.d  = bcd2dec(Wire.read() & 0x3F);
  rtc.mo = bcd2dec(Wire.read() & 0x1F);
  rtc.y  = 2000 + bcd2dec(Wire.read());

  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)1) == 1)
    rtc.osf = Wire.read() & 0x80;

  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x11);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)2) == 2) {
    int8_t msb = (int8_t)Wire.read();
    uint8_t lsb = Wire.read();
    rtc.temp = msb + (lsb >> 6) * 0.25f;
  }
  rtc.ok = true;
}

bool rtcSet(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x00);
  Wire.write(dec2bcd(s)); Wire.write(dec2bcd(mi)); Wire.write(dec2bcd(h));
  Wire.write(1);
  Wire.write(dec2bcd(d)); Wire.write(dec2bcd(mo)); Wire.write(dec2bcd(y % 100));
  if (Wire.endTransmission() != 0) return false;
  // skasuj flage OSF
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)ADDR_DS3231, (uint8_t)1) != 1) return false;
  uint8_t st = Wire.read() & 0x7F;
  Wire.beginTransmission(ADDR_DS3231);
  Wire.write(0x0F); Wire.write(st);
  return Wire.endTransmission() == 0;
}

// ---------------- 24C02 ----------------
int eeRead(uint8_t addr) {
  Wire.beginTransmission(ADDR_EEPROM);
  Wire.write(addr);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)ADDR_EEPROM, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

bool eeWrite(uint8_t addr, uint8_t v) {
  Wire.beginTransmission(ADDR_EEPROM);
  Wire.write(addr); Wire.write(v);
  if (Wire.endTransmission() != 0) return false;
  for (uint8_t i = 0; i < 20; i++) {             // ACK polling (max ~20 ms)
    delay(1);
    if (i2cPresent(ADDR_EEPROM)) return true;
  }
  return false;
}

String eepromTest(bool &ok) {
  ok = false;
  const uint8_t addr = 0xFF;
  int orig = eeRead(addr);
  if (orig < 0) return "BLAD: EEPROM nie odpowiada";
  uint8_t pattern = (uint8_t)~orig;
  if (!eeWrite(addr, pattern)) return "BLAD: zapis nieudany";
  int rd = eeRead(addr);
  eeWrite(addr, (uint8_t)orig);
  int back = eeRead(addr);
  char b[96];
  snprintf(b, sizeof(b), "adres 0x%02X: oryg=0x%02X zapis=0x%02X odczyt=0x%02X przywrocono=0x%02X",
           addr, orig, pattern, rd, back);
  ok = (rd == pattern && back == orig);
  return String(ok ? "OK - " : "BLAD - ") + b;
}

// =====================================================================
//                          SD / 1-Wire
// =====================================================================
bool sdMount() {
  SD.end();
  sdMounted = SD.begin(SD_CS, sdSPI, 4000000);
  sdSizeMB = sdMounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0;
  return sdMounted;
}

String sdTest(bool &ok) {
  ok = false;
  if (!sdMounted && !sdMount()) return "BLAD: brak karty lub nie mozna zamontowac";
  String payload = "KinCony B8M test " + String(millis());
  File f = SD.open("/b8m_test.txt", FILE_WRITE);
  if (!f) { sdMounted = false; return "BLAD: nie mozna otworzyc pliku do zapisu"; }
  f.print(payload);
  f.close();
  f = SD.open("/b8m_test.txt", FILE_READ);
  if (!f) return "BLAD: nie mozna otworzyc pliku do odczytu";
  String rd = f.readString();
  f.close();
  ok = (rd == payload);
  return String(ok ? "OK" : "BLAD") + " - zapisano/odczytano: \"" + rd + "\" (karta " + String((uint32_t)sdSizeMB) + " MB)";
}

void owScan() {
  for (uint8_t b = 0; b < 4; b++) {
    dtBus[b]->begin();
    dtBus[b]->setWaitForConversion(false);
    owCount[b] = dtBus[b]->getDeviceCount();
    if (owCount[b] > 4) owCount[b] = 4;
    for (uint8_t i = 0; i < 4; i++) owTemp[b][i] = -127;
    if (owCount[b]) dtBus[b]->requestTemperatures();
  }
}

void owUpdate() {           // wywolywane co 2 s: odczyt poprzedniej konwersji + nowa
  for (uint8_t b = 0; b < 4; b++) {
    if (!owCount[b]) continue;
    for (uint8_t i = 0; i < owCount[b]; i++) owTemp[b][i] = dtBus[b]->getTempCByIndex(i);
    dtBus[b]->requestTemperatures();
  }
}

// =====================================================================
//                          UART: RS485 / Tuya
// =====================================================================
void pushByte(uint8_t *buf, uint16_t &len, uint8_t c) {
  if (len >= 256) { memmove(buf, buf + 64, 192); len = 192; }
  buf[len++] = c;
}

String toHex(const uint8_t *buf, uint16_t len) {
  String s; s.reserve(len * 3);
  char b[4];
  for (uint16_t i = 0; i < len; i++) { snprintf(b, sizeof(b), "%02X ", buf[i]); s += b; }
  return s;
}

String toAsciiSafe(const uint8_t *buf, uint16_t len) {
  String s; s.reserve(len);
  for (uint16_t i = 0; i < len; i++) {
    char c = buf[i];
    s += (c >= 32 && c < 127 && c != '"' && c != '\\') ? c : '.';
  }
  return s;
}

void uartPoll() {
  while (RS485.available()) pushByte(rsBuf, rsLen, RS485.read());
  while (TuyaSer.available()) pushByte(tuyaBuf, tuyaLen, TuyaSer.read());
}

// =====================================================================
//                          GPIO
// =====================================================================
void gpioApply(FreeGpio &g) {
  if (g.out) { pinMode(g.pin, OUTPUT); digitalWrite(g.pin, g.val); }
  else pinMode(g.pin, INPUT_PULLUP);
}

// =====================================================================
//                          OLED
// =====================================================================
void oledUpdate() {
  if (!oled.ok) return;
  if (oledMode == OLED_FILL) { oled.fill(0xFF); oled.display(); return; }
  if (oledMode == OLED_CLEAR) { oled.clear(); oled.display(); return; }

  char l[24];
  oled.clear();
  oled.text(0, 0, "KinCony B8M test " FW_VERSION);
  snprintf(l, sizeof(l), "AP %s", WiFi.softAPIP().toString().c_str());
  oled.text(0, 1, l);
  if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0))
    snprintf(l, sizeof(l), "ETH %s", ETH.localIP().toString().c_str());
  else snprintf(l, sizeof(l), "ETH %s", ETH.linkUp() ? "brak IP" : "brak kabla");
  oled.text(0, 2, l);

  char o[9], d[9];
  for (uint8_t i = 0; i < 8; i++) { o[i] = (outState >> i & 1) ? '1' : '0'; d[i] = (diState >> i & 1) ? '1' : '0'; }
  o[8] = d[8] = 0;
  snprintf(l, sizeof(l), "OUT %s", o); oled.text(0, 3, l);
  snprintf(l, sizeof(l), "DI  %s", d); oled.text(0, 4, l);
  snprintf(l, sizeof(l), "A1 %.2f A2 %.2f", adcV[0], adcV[1]); oled.text(0, 5, l);
  snprintf(l, sizeof(l), "A3 %.2f A4 %.2f", adcV[2], adcV[3]); oled.text(0, 6, l);
  if (rtc.ok) snprintf(l, sizeof(l), "%04u-%02u-%02u %02u:%02u:%02u", rtc.y, rtc.mo, rtc.d, rtc.h, rtc.mi, rtc.s);
  else snprintf(l, sizeof(l), "RTC brak");
  oled.text(0, 7, l);
  oled.display();
}

// =====================================================================
//                          WWW
// =====================================================================
void sendJson(const String &s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void sendResult(bool ok, const String &msg) {
  String m = msg; m.replace("\"", "'");
  sendJson(String("{\"ok\":") + (ok ? "true" : "false") + ",\"result\":\"" + m + "\"}");
}

static String fnum(float v, uint8_t dec) {
  if (isnan(v) || isinf(v)) return "0";
  return String(v, (unsigned int)dec);
}

void handleStatus() {
  String j; j.reserve(3000);
  j += "{\"fw\":\"" FW_VERSION "\"";
  j += ",\"up\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"pcf\":" + String(pcfOk ? "true" : "false");
  j += ",\"chase\":" + String(chaseOn ? "true" : "false");
  j += ",\"out\":[";
  for (uint8_t i = 0; i < 8; i++) { if (i) j += ','; j += (outState >> i & 1) ? '1' : '0'; }
  j += "],\"di\":[";
  for (uint8_t i = 0; i < 8; i++) { if (i) j += ','; j += (diState >> i & 1) ? '1' : '0'; }
  j += "],\"btn0\":" + String(digitalRead(PIN_BTN_DOWNLOAD) == LOW ? "true" : "false");
  j += ",\"ads\":" + String(adsOk ? "true" : "false");
  j += ",\"adc\":[";
  for (uint8_t i = 0; i < 4; i++) { if (i) j += ','; j += fnum(adcV[i], 4); }
  j += "],\"raw\":[";
  for (uint8_t i = 0; i < 4; i++) { if (i) j += ','; j += String(adcRaw[i]); }
  j += "]";

  char t[24];
  snprintf(t, sizeof(t), "%04u-%02u-%02u %02u:%02u:%02u", rtc.y, rtc.mo, rtc.d, rtc.h, rtc.mi, rtc.s);
  j += ",\"rtc\":{\"ok\":" + String(rtc.ok ? "true" : "false") + ",\"time\":\"" + t + "\",\"temp\":" +
       fnum(rtc.temp, 2) + ",\"osf\":" + String(rtc.osf ? "true" : "false") + "}";

  j += ",\"oled\":" + String(oled.ok ? "true" : "false");
  j += ",\"sd\":{\"cd\":" + String(digitalRead(SD_CD)) + ",\"mounted\":" + String(sdMounted ? "true" : "false") +
       ",\"sizeMB\":" + String((uint32_t)sdSizeMB) + "}";

  j += ",\"ow\":[";
  for (uint8_t b = 0; b < 4; b++) {
    if (b) j += ',';
    j += "{\"pin\":" + String(OW_PINS[b]) + ",\"n\":" + String(owCount[b]) + ",\"t\":[";
    for (uint8_t i = 0; i < owCount[b]; i++) { if (i) j += ','; j += fnum(owTemp[b][i], 2); }
    j += "]}";
  }
  j += "],\"gpio\":[";
  for (uint8_t i = 0; i < 3; i++) {
    if (i) j += ',';
    j += "{\"pin\":" + String(gpioCfg[i].pin) + ",\"out\":" + String(gpioCfg[i].out ? "true" : "false") +
         ",\"lvl\":" + String(digitalRead(gpioCfg[i].pin)) + "}";
  }
  j += "],\"rs485\":{\"ascii\":\"" + toAsciiSafe(rsBuf, rsLen) + "\",\"hex\":\"" + toHex(rsBuf, rsLen) + "\"}";
  j += ",\"tuya\":\"" + toHex(tuyaBuf, tuyaLen) + "\"";

  j += ",\"net\":{\"ssid\":\"" + String(AP_SSID) + "\",\"apip\":\"" + WiFi.softAPIP().toString() +
       "\",\"clients\":" + String(WiFi.softAPgetStationNum()) + ",\"mdns\":\"" + String(MDNS_NAME) + ".local\"" +
       ",\"eth\":" + String(ethStarted ? "true" : "false") + ",\"link\":" + String(ETH.linkUp() ? "true" : "false") +
       ",\"speed\":" + String(ETH.linkUp() ? ETH.linkSpeed() : 0) + ",\"ethip\":\"" + ETH.localIP().toString() +
       "\",\"ethmac\":\"" + ETH.macAddress() + "\"}";

  j += ",\"i2c\":\"" + i2cList + "\"";
  j += ",\"self\":[";
  for (uint8_t i = 0; i < selfCount; i++) {
    if (i) j += ',';
    String info = selfTests[i].info; info.replace("\"", "'");
    j += "[\"" + String(selfTests[i].name) + "\"," + (selfTests[i].ok ? "true" : "false") + ",\"" + info + "\"]";
  }
  j += "]}";
  sendJson(j);
}

void handleOut() {
  String ch = server.arg("ch"), v = server.arg("v");
  chaseOn = false;
  if (ch == "all") outState = (v == "1") ? 0xFF : 0x00;
  else {
    int n = ch.toInt();
    if (n < 1 || n > 8) { sendResult(false, "zly kanal"); return; }
    uint8_t m = 1 << (n - 1);
    if (v == "t") outState ^= m; else if (v == "1") outState |= m; else outState &= ~m;
  }
  bool ok = pcfWrite();
  sendResult(ok, ok ? "OK" : "PCF8575 nie odpowiada");
}

void handleChase() {
  chaseOn = server.arg("on") == "1";
  chaseIdx = 0; chaseT = 0;
  if (!chaseOn) { outState = 0; pcfWrite(); }
  sendResult(true, chaseOn ? "chase start" : "chase stop");
}

void handleRtcSet() {
  bool ok = rtcSet(server.arg("y").toInt(), server.arg("mo").toInt(), server.arg("d").toInt(),
                   server.arg("h").toInt(), server.arg("mi").toInt(), server.arg("s").toInt());
  rtcRead();
  sendResult(ok, ok ? "czas ustawiony" : "blad zapisu RTC");
}

void handleEeprom() { bool ok; String r = eepromTest(ok); sendResult(ok, r); }
void handleSd()     { bool ok; String r = sdTest(ok); sendResult(ok, r); }
void handleScan()   { i2cScan(); sendResult(true, i2cList); }
void handleOwScan() { owScan(); sendResult(true, "OK"); }

void handleOled() {
  String m = server.arg("mode");
  if (m == "fill") oledMode = OLED_FILL;
  else if (m == "clear") oledMode = OLED_CLEAR;
  else if (m == "invert") { oledInv = !oledInv; oled.invert(oledInv); }
  else { oledMode = OLED_STATUS; if (!oled.ok) oled.begin(Wire, ADDR_OLED); }
  oledUpdate();
  sendResult(oled.ok, oled.ok ? "OK" : "OLED nie odpowiada");
}

void handleGpio() {
  int pin = server.arg("pin").toInt();
  for (auto &g : gpioCfg) {
    if (g.pin == pin) {
      g.out = server.arg("mode") == "out";
      g.val = server.arg("v") == "1";
      gpioApply(g);
      sendResult(true, "OK");
      return;
    }
  }
  sendResult(false, "niedozwolony pin");
}

void handleRs485Send() {
  uint32_t b = server.arg("baud").toInt();
  if (b >= 1200 && b <= 921600 && b != rsBaud) {
    rsBaud = b;
    RS485.updateBaudRate(rsBaud);
  }
  String d = server.arg("data");
  RS485.print(d);
  RS485.print("\r\n");
  RS485.flush();
  sendResult(true, "wyslano " + String(d.length() + 2) + " B @ " + String(rsBaud));
}

void handleTuyaSend() {
  String h = server.arg("hex");
  uint8_t out[64]; uint8_t n = 0; int nib = -1;
  for (size_t i = 0; i < h.length() && n < sizeof(out); i++) {
    char c = h[i]; int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;
    if (nib < 0) nib = v; else { out[n++] = (nib << 4) | v; nib = -1; }
  }
  TuyaSer.write(out, n);
  sendResult(true, "wyslano " + String(n) + " B");
}

void setupWeb() {
  server.on("/", []() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/status", handleStatus);
  server.on("/api/out", handleOut);
  server.on("/api/chase", handleChase);
  server.on("/api/rtc/set", handleRtcSet);
  server.on("/api/eeprom/test", handleEeprom);
  server.on("/api/sd/test", handleSd);
  server.on("/api/scan", handleScan);
  server.on("/api/ow/scan", handleOwScan);
  server.on("/api/oled", handleOled);
  server.on("/api/gpio", handleGpio);
  server.on("/api/rs485/send", handleRs485Send);
  server.on("/api/rs485/clear", []() { rsLen = 0; sendResult(true, "OK"); });
  server.on("/api/tuya/send", handleTuyaSend);
  server.on("/api/tuya/clear", []() { tuyaLen = 0; sendResult(true, "OK"); });
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });
  server.begin();
}

// =====================================================================
//                          Autotest
// =====================================================================
void addSelf(const char *name, bool ok, const String &info) {
  if (selfCount >= 12) return;
  selfTests[selfCount++] = {name, ok, info};
  Serial.printf("[TEST] %-22s %s  %s\n", name, ok ? "OK  " : "BLAD", info.c_str());
}

void runSelfTest() {
  selfCount = 0;
  i2cScan();
  addSelf("Skan I2C", i2cList.length() > 0, i2cList.length() ? i2cList : String("brak urzadzen"));
  addSelf("PCF8575 @0x22", pcfWrite() && pcfRead(), pcfOk ? "wyjscia OFF, wejscia odczytane" : "brak odpowiedzi");
  adsReadAll();
  addSelf("ADS1115 @0x48", adsOk, adsOk ? "A1=" + fnum(adcV[0], 3) + " V" : String("brak odpowiedzi"));
  rtcRead();
  addSelf("DS3231 @0x68", rtc.ok, rtc.ok ? (rtc.osf ? String("dziala, ale flaga OSF - ustaw czas") : String("dziala")) : String("brak odpowiedzi"));
  bool eok; String er = eepromTest(eok);
  addSelf("EEPROM 24C02 @0x50", eok, er);
  addSelf("OLED SSD1306 @0x3C", oled.ok, oled.ok ? "zainicjowany" : "brak odpowiedzi");
  addSelf("Karta SD", sdMounted, sdMounted ? String((uint32_t)sdSizeMB) + " MB" : String("brak karty / blad"));
  uint8_t owTotal = owCount[0] + owCount[1] + owCount[2] + owCount[3];
  addSelf("1-Wire", owTotal > 0, String(owTotal) + " czujnikow DS18B20");
  addSelf("Ethernet W5500", ethStarted, ethStarted ? "sterownik uruchomiony, MAC " + ETH.macAddress() : String("blad inicjalizacji"));
  addSelf("WiFi AP", WiFi.softAPIP() != IPAddress(0, 0, 0, 0), String(AP_SSID) + " / " + WiFi.softAPIP().toString());
}

// =====================================================================
//                          setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== KinCony B8M - program testowy v" FW_VERSION " ===");

  pinMode(PIN_BTN_DOWNLOAD, INPUT_PULLUP);
  pinMode(SD_CD, INPUT_PULLUP);
  for (uint8_t i = 0; i < 3; i++) { gpioCfg[i] = {GPIO_FREE[i], false, false}; gpioApply(gpioCfg[i]); }

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  Wire.setTimeOut(50);
  pcfWrite();                                  // wszystkie wyjscia OFF jak najszybciej
  oled.begin(Wire, ADDR_OLED);
  if (oled.ok) { oled.text(0, 0, "KinCony B8M test"); oled.text(0, 2, "Start..."); oled.display(); }

  RS485.begin(rsBaud, SERIAL_8N1, RS485_RX, RS485_TX);
  TuyaSer.begin(9600, SERIAL_8N1, TUYA_RX, TUYA_TX);

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdMount();

  owScan();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  Serial.printf("WiFi AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  ethStarted = ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_INT, ETH_RST, SPI3_HOST, ETH_SCK, ETH_MISO, ETH_MOSI);
  Serial.printf("Ethernet W5500: %s\n", ethStarted ? "OK" : "BLAD");

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
  } else Serial.println("mDNS: BLAD");

  setupWeb();
  runSelfTest();
  Serial.println("Strona WWW: http://192.168.4.1  lub  http://b8m-test.local");
}

void loop() {
  server.handleClient();
  uartPoll();

  uint32_t now = millis();
  static uint32_t tDi = 0, tAdc = 0, tRtc = 0, tOw = 0, tOled = 0, tEth = 0;
  static bool ethHadIp = false;

  if (now - tDi >= 50) { tDi = now; pcfRead(); }

  if (chaseOn && now - chaseT >= 300) {
    chaseT = now;
    outState = 1 << chaseIdx;
    chaseIdx = (chaseIdx + 1) % 8;
    pcfWrite();
  }

  if (now - tAdc >= 500) { tAdc = now; adsReadAll(); }
  if (now - tRtc >= 1000) { tRtc = now; rtcRead(); }
  if (now - tOw >= 2000) { tOw = now; owUpdate(); }
  if (now - tOled >= 500) { tOled = now; oledUpdate(); }

  if (now - tEth >= 1000) {
    tEth = now;
    bool hasIp = ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0);
    if (hasIp && !ethHadIp) Serial.printf("Ethernet IP: %s (%u Mbps)\n", ETH.localIP().toString().c_str(), ETH.linkSpeed());
    ethHadIp = hasIp;
  }
}
