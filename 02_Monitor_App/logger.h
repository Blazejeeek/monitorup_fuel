// MonitorUp - karta SD: dzienne logi CSV, dziennik zdarzen, rotacja, historia minutowa 24 h w PSRAM
#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "config.h"

struct EventRec { uint32_t t; char type[12]; char msg[96]; };
#define EV_RAM 60
#define HIST_N 1440
struct LogCols { int cm[TANK_COUNT], l[TANK_COUNT]; };   // indeksy kolumn w pliku CSV (-1 = brak)

extern bool sdMounted;
extern EventRec evRam[EV_RAM];
extern uint8_t evHead, evCount;
extern uint32_t curMin;            // biezaca minuta (epoch/60)

bool timeValid();
void fmtTime(char *b, size_t n, const char *fmt, time_t t = 0);
String numPL(float v, int dec);    // liczba z przecinkiem dziesietnym
float parsePL(const String &s);
bool sdMount();
void ensureDir(const String &path);
void logEvent(const char *type, const String &msg);
float histGet(uint8_t i, uint32_t m);
bool histTick();                   // co 1 s; true = zamknieto minute
uint32_t histLastClosed();
String logPath(time_t t);
String logHeader();
void splitSemi(const String &l, std::vector<String> &v);
LogCols logCols(const String &hdr);
void logRow();
void logRotate();
void sdLoop();
String sdTest(JsonObject r);
void histPrefill();
void loggerBegin();
