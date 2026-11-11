// MonitorUp - wyswietlacz OLED SSD1306 128x64 (U8g2)
#pragma once
#include <Arduino.h>

extern bool oledOk;
extern uint32_t oledResetHoldMs;   // przytrzymanie przycisku DOWNLOAD (odliczanie resetu)

void oledBegin();
void oledBoot(const char *bottom, uint8_t phase);   // ekran startowy z logo
void oledLoop();                   // co 500 ms
