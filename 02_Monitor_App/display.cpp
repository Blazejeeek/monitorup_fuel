// MonitorUp - wyswietlacz OLED SSD1306 128x64 (U8g2)
#include "display.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <ETH.h>
#include "hw.h"
#include "config.h"
#include "sensors.h"
#include "net.h"
#include "logger.h"
#include "stats.h"
#include "outputs.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
bool oledOk = false;

#define WWW_ADDR   "www.monitorup.pl"

// Wysylanie po jednym wierszu (8 px): krotkie blokady I2C, zeby PWM wyjsc nie migotal
void oledSend() {
  if (!oledOk) return;
  for (uint8_t r = 0; r < 8; r++) {
    I2CLock l;
    u8g2.updateDisplayArea(0, r, 16, 1);
  }
}

uint32_t oledResetHoldMs = 0;     // przytrzymanie przycisku Download (reset ustawien)

void oledBegin() {
  oledOk = i2cPresent(ADDR_OLED);
  if (!oledOk) return;
  I2CLock l;
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.enableUTF8Print();
}

// Ikona zbiornika z ciecza i fala; level 0..1
void drawTankIcon(int x, int y, int w, int h, float level, uint8_t phase) {
  u8g2.drawRFrame(x, y, w, h, 3);
  int innerTop = y + 2, innerBot = y + h - 3;
  int lvlY = innerBot - (int)((innerBot - innerTop) * constrain(level, 0.0f, 1.0f));
  for (int i = x + 2; i <= x + w - 3; i++) {
    int wy = lvlY + (int)roundf(1.5f * sinf((i - x + phase) * 0.6f));
    if (wy < innerTop) wy = innerTop;
    if (wy <= innerBot) u8g2.drawVLine(i, wy, innerBot - wy + 1);
  }
}

// Ekran startowy: nazwa MonitorUp.pl w jednej linii, wersje SW i HW + adres strony; bottom = dolny pasek (np. test wyjsc)
void oledBoot(const char *bottom, uint8_t phase) {
  if (!oledOk) return;
  u8g2.clearBuffer();
  drawTankIcon(2, 3, 26, 38, 0.6f, phase);
  // nazwa w jednej linii: najwieksza czcionka, ktora miesci sie obok ikony (x 34..127)
  static const uint8_t *const fonts[] = {u8g2_font_helvB14_tf, u8g2_font_helvB12_tf, u8g2_font_helvB10_tf, u8g2_font_helvB08_tf};
  for (const uint8_t *f : fonts) { u8g2.setFont(f); if (u8g2.getStrWidth("MonitorUp.pl") <= 93) break; }
  u8g2.drawStr(34, 20, "MonitorUp.pl");
  u8g2.setFont(u8g2_font_helvR08_tf);
  u8g2.drawStr(34, 31, "SW " FW_VERSION);
  u8g2.drawStr(34, 41, "HW " HW_VERSION " (B8M)");
  u8g2.drawHLine(0, 44, 128);
  u8g2.setFont(u8g2_font_helvR08_tf);
  int w = u8g2.getStrWidth(WWW_ADDR);
  u8g2.drawStr((128 - w) / 2, 54, WWW_ADDR);
  if (bottom && *bottom) {
    u8g2.setFont(u8g2_font_5x7_tf);
    w = u8g2.getStrWidth(bottom);
    u8g2.drawStr((128 - w) / 2, 63, bottom);
  }
  oledSend();
}

static void drawHeader() {
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr(0, 8, "MonitorUp.pl");
  u8g2.setFont(u8g2_font_5x7_tf);
  String ic;
  if (ethHasIp()) ic += "ETH ";
  if (wifiHasIp()) ic += "WiFi ";
  if (apOn) ic += "AP";
  ic.trim();
  u8g2.drawStr(128 - u8g2.getStrWidth(ic.c_str()), 7, ic.c_str());
  u8g2.drawHLine(0, 10, 128);
}

static void drawFooter() {
  u8g2.drawHLine(0, 54, 128);
  u8g2.setFont(u8g2_font_5x7_tf);
  char b[24];
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    strftime(b, sizeof(b), "%d.%m.%Y %H:%M:%S", &t);
  } else strcpy(b, "czas nieustawiony");
  u8g2.drawStr(0, 63, b);
  const char *sd = sdMounted ? "SD" : "--";
  u8g2.drawStr(128 - u8g2.getStrWidth(sd), 63, sd);
}

static void fmtComma(char *buf, size_t n, float v, int dec) {
  snprintf(buf, n, "%.*f", dec, v);
  for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
}

void oledTank(uint8_t i) {
  TankState s = tankGet(i);
  const TankCal &c = cfg.tank[i];
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_helvR08_te);
  u8g2.drawUTF8(0, 21, cfg.tankName[i]);

  // pionowy slupek zbiornika z prawej
  int bx = 112, by = 13, bw = 14, bh = 39;
  u8g2.drawFrame(bx, by, bw, bh);
  if (s.valid) {
    int fh = (int)((bh - 4) * s.pct / 100.0f);
    if (fh > 0) u8g2.drawBox(bx + 2, by + bh - 2 - fh, bw - 4, fh);
    // znaczniki progow
    if (c.heightCm > 0) {
      for (float th : {c.thrCrit, c.thrMin, c.thrHigh}) {
        int ty = by + bh - 2 - (int)((bh - 4) * constrain(th / c.heightCm, 0.0f, 1.0f));
        u8g2.drawHLine(bx - 3, ty, 3);
      }
    }
  }

  char b[24];
  if (s.valid) {
    fmtComma(b, sizeof(b), s.cm, 1);
    u8g2.setFont(u8g2_font_logisoso20_tn);
    int w = u8g2.getStrWidth(b);
    u8g2.drawStr(0, 43, b);
    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.drawStr(w + 3, 43, "cm");
    u8g2.setFont(u8g2_font_helvR08_te);
    char p[20];
    if (s.vol >= 0) snprintf(p, sizeof(p), "%.0f%% %.0fl", s.pct, s.vol); else snprintf(p, sizeof(p), "%.0f%%", s.pct);
    u8g2.drawStr(0, 52, p);
    uint8_t e = tankEffState(i);
    String stn = stateName(e);
    if (isAlarmState(e) && alarmAcked) stn = "SKWITOWANY";
    int px = u8g2.getStrWidth(p) + 5;
    if (px + u8g2.getUTF8Width(stn.c_str()) > 108) { u8g2.setFont(u8g2_font_5x7_tf); if (e == ST_HIGH) stn = "ZA DUZY"; else if (e == ST_DROP) stn = "SPADEK"; }
    u8g2.drawUTF8(px, 52, stn.c_str());
  } else {
    u8g2.setFont(u8g2_font_helvB08_te);                 // mniejsza czcionka: napis nie wchodzi na slupek zbiornika (x >= 109)
    u8g2.drawUTF8(0, 38, s.state == ST_OFF ? "WYŁĄCZONY" : "BŁĄD CZUJNIKA");
    u8g2.setFont(u8g2_font_5x7_tf);
    if (c.type == 0 && s.state == ST_FAULT) {
      fmtComma(b, sizeof(b), s.mA, 2);
      String t = String("prad ") + b + " mA";
      u8g2.drawStr(0, 51, t.c_str());
    }
  }
  drawFooter();
}

void oledNet() {
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_5x7_tf);
  String l1 = "ETH: " + String(!cfg.ethEn ? "wylaczony" : ethHasIp() ? ETH.localIP().toString().c_str() : (ETH.linkUp() ? "brak IP" : "brak kabla"));
  String l2 = "WiFi: " + String(!cfg.wifiEn ? "wylaczone" : wifiHasIp() ? (WiFi.localIP().toString() + " " + String(WiFi.RSSI()) + "dBm").c_str() : "laczenie...");
  String l3 = "AP: " + String(apOn ? cfg.apSsid : "wylaczony");
  String l4 = apOn ? "AP IP: " + WiFi.softAPIP().toString() : String("");
  u8g2.drawStr(0, 18, l1.c_str());
  u8g2.drawStr(0, 26, l2.c_str());
  u8g2.drawStr(0, 34, l3.c_str());
  u8g2.drawStr(0, 42, l4.c_str());
  u8g2.drawStr(0, 50, "SW " FW_VERSION "  HW " HW_VERSION " B8M");
  drawFooter();
}

// Wywolywane co 500 ms: rotacja ekranow co 5 s, przy alarmie KRYTYCZNY/ZA DUZY ekran zbiornika miga
void oledLoop() {
  if (!oledOk) return;
  static uint8_t screen = 0;
  static uint32_t tSwitch = 0;
  static bool blink = false;
  blink = !blink;

  int alarmTank = -1;
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    if (isAlarmState(tankEffState(i))) { alarmTank = i; break; }
  }

  if (oledResetHoldMs) {                 // odliczanie resetu ustawien
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_te);
    u8g2.drawUTF8(0, 14, "RESET USTAWIEŃ");
    u8g2.setFont(u8g2_font_helvR08_te);
    u8g2.drawUTF8(0, 30, "Trzymaj przycisk...");
    char b[16]; snprintf(b, sizeof(b), "%u", (unsigned)((10000 - min<uint32_t>(oledResetHoldMs, 10000)) / 1000 + 1));
    u8g2.setFont(u8g2_font_logisoso20_tn);
    u8g2.drawStr(0, 60, b + 0);
    oledSend();
    return;
  }

  // przyciemnienie po N min bez zmian alarmu
  static uint32_t lastActivity = 0; static bool dim = false; static bool lastAl = false;
  bool al = alarmTank >= 0;
  if (al != lastAl || !lastActivity) { lastActivity = millis(); lastAl = al; }
  bool wantDim = cfg.oledDimMin && !al && millis() - lastActivity > (uint32_t)cfg.oledDimMin * 60000UL;
  if (wantDim != dim) { dim = wantDim; I2CLock l; u8g2.setContrast(dim ? 10 : 255); }

  if (alarmTank >= 0) {
    oledTank(alarmTank);
    u8g2.setDrawColor(2);                 // XOR = inwersja
    if (blink && !alarmAcked) u8g2.drawBox(0, 0, 128, 64);
    u8g2.setDrawColor(1);
    oledSend();
    return;
  }

  if (millis() - tSwitch >= 5000) {
    tSwitch = millis();
    for (uint8_t k = 0; k < TANK_COUNT + 1; k++) {
      screen = (screen + 1) % (TANK_COUNT + 1);
      if (screen == TANK_COUNT || cfg.tank[screen].enabled) break;
    }
  }
  if (screen < TANK_COUNT) oledTank(screen); else oledNet();
  oledSend();
}
