// Monitor App - karta SD: dzienne logi CSV, dziennik zdarzen, rotacja, historia minutowa w PSRAM
#include "logger.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include "hw.h"
#include "config.h"
#include "sensors.h"

SPIClass sdSPI(FSPI);
bool sdMounted = false;
static uint32_t sdLastTry = 0;

bool timeValid() { return time(nullptr) > 1700000000; }

void fmtTime(char *b, size_t n, const char *fmt, time_t t) {
  if (!t) t = time(nullptr);
  struct tm tm; localtime_r(&t, &tm);
  strftime(b, n, fmt, &tm);
}

// Liczba z przecinkiem dziesietnym (CSV dla polskiego Excela)
String numPL(float v, int dec) {
  char b[24]; snprintf(b, sizeof(b), "%.*f", dec, v);
  for (char *p = b; *p; p++) if (*p == '.') *p = ',';
  return b;
}

// Montowanie bez uzaleznienia od pinu CD (polaryzacja wykrywania karty na B8M niepewna)
static const uint32_t SD_FREQS[] = {20000000, 10000000, 4000000, 1000000};
static uint32_t sdFreq = 0;
bool sdMount() {
  SD.end();
  sdMounted = false;
  for (uint32_t f : SD_FREQS) {
    if (SD.begin(SD_CS, sdSPI, f) && SD.cardType() != CARD_NONE) { sdMounted = true; sdFreq = f; break; }
    SD.end();
  }
  return sdMounted;
}

void ensureDir(const String &path) {         // tworzy katalogi dla sciezki pliku
  int p = 1;
  while ((p = path.indexOf('/', p)) > 0) {
    String d = path.substring(0, p);
    if (!SD.exists(d)) SD.mkdir(d);
    p++;
  }
}

// ---------------- Dziennik zdarzen ----------------
EventRec evRam[EV_RAM];
uint8_t evHead = 0, evCount = 0;

void logEvent(const char *type, const String &msg) {
  EventRec &e = evRam[evHead];
  e.t = time(nullptr);
  strlcpy(e.type, type, sizeof(e.type));
  strlcpy(e.msg, msg.c_str(), sizeof(e.msg));
  evHead = (evHead + 1) % EV_RAM;
  if (evCount < EV_RAM) evCount++;
  Serial.printf("[ZDARZENIE] %s: %s\n", type, msg.c_str());
  if (!sdMounted || !timeValid()) return;
  char fn[32]; fmtTime(fn, sizeof(fn), "/events/%Y-%m.csv");
  bool isNew = !SD.exists(fn);
  if (isNew) ensureDir(fn);
  File f = SD.open(fn, FILE_APPEND);
  if (!f) { sdMounted = false; return; }
  if (isNew) f.println("data;czas;typ;opis");
  char d[24]; fmtTime(d, sizeof(d), "%Y-%m-%d;%H:%M:%S");
  String m = msg; m.replace(";", ",");
  f.printf("%s;%s;%s\n", d, type, m.c_str());
  f.close();
}

// ---------------- Historia minutowa (PSRAM, 24 h) ----------------
static float *histCm[TANK_COUNT];          // NAN = brak danych
static uint32_t histStartMin = 0;          // numer minuty (epoch/60) slotu 0... przesuwany kolowo
static double minSum[TANK_COUNT]; static uint16_t minCnt[TANK_COUNT];
uint32_t curMin = 0;

static int histIdx(uint32_t m) { return m % HIST_N; }
float histGet(uint8_t i, uint32_t m) {       // m = epoch/60
  if (!histCm[i] || !curMin || m > curMin || curMin - m >= HIST_N) return NAN;
  return histCm[i][histIdx(m)];
}
static void histPut(uint8_t i, uint32_t m, float v) { if (histCm[i]) histCm[i][histIdx(m)] = v; }

void histBegin() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    histCm[i] = (float *)ps_malloc(HIST_N * sizeof(float));
    if (!histCm[i]) histCm[i] = (float *)malloc(HIST_N * sizeof(float));
    for (int k = 0; k < HIST_N; k++) histCm[i][k] = NAN;
  }
}

// Wywolywane co 1 s; zwraca true, gdy zamknieto pelna minute (wtedy dostepna srednia minutowa)
bool histTick() {
  if (!timeValid()) return false;
  uint32_t m = time(nullptr) / 60;
  bool closed = false;
  if (curMin && m != curMin) {
    for (uint8_t i = 0; i < TANK_COUNT; i++) histPut(i, curMin, minCnt[i] ? (float)(minSum[i] / minCnt[i]) : NAN);
    for (uint32_t g = curMin + 1; g < m && g - curMin < HIST_N; g++)          // luka (np. zmiana czasu)
      for (uint8_t i = 0; i < TANK_COUNT; i++) histPut(i, g, NAN);
    closed = true;
  }
  if (m != curMin) { for (uint8_t i = 0; i < TANK_COUNT; i++) { minSum[i] = 0; minCnt[i] = 0; } }
  uint32_t prev = curMin;
  curMin = m;
  if (prev && m < prev) for (uint8_t i = 0; i < TANK_COUNT; i++) for (int k = 0; k < HIST_N; k++) histCm[i][k] = NAN;  // czas cofniety
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    TankState s = tankGet(i);
    if (s.valid) { minSum[i] += s.cm; minCnt[i]++; }
  }
  return closed;
}
uint32_t histLastClosed() { return curMin ? curMin - 1 : 0; }

// ---------------- Dzienne logi CSV ----------------
// Kolumny zalezne od wlaczonych zbiornikow: data;czas;z1_cm;z1_proc;z1_l;z1_mA;z1_stan[;z2_...];alarm_skwitowany
// Starsze pliki (ppoz_*/paliwo_*) sa czytane przez mape naglowka.
String logPath(time_t t) { char b[40]; fmtTime(b, sizeof(b), "/logs/%Y/%m/%Y-%m-%d.csv", t); return b; }

uint8_t tankEffState(uint8_t i);   // z modulu alarmow
extern bool alarmAcked;

String logHeader() {
  String h = "data;czas";
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    if (!cfg.tank[i].enabled) continue;
    String z = ";z" + String(i + 1) + "_";
    h += z + "cm" + z + "proc" + z + "l" + z + "mA" + z + "stan";
  }
  return h + ";alarm_skwitowany";
}

void splitSemi(const String &l, std::vector<String> &v) {
  v.clear(); int pos = 0, q;
  while ((q = l.indexOf(';', pos)) >= 0) { v.push_back(l.substring(pos, q)); pos = q + 1; }
  String last = l.substring(pos); last.trim(); v.push_back(last);
}

LogCols logCols(const String &hdr) {
  LogCols c; for (uint8_t i = 0; i < TANK_COUNT; i++) { c.cm[i] = -1; c.l[i] = -1; }
  std::vector<String> v; splitSemi(hdr, v);
  for (int k = 0; k < (int)v.size(); k++) {
    const String &n = v[k];
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      String z = "z" + String(i + 1) + "_";
      const char *old = i == 0 ? "ppoz_" : "paliwo_";       // format wersji 0.2.0
      if (n == z + "cm" || n == String(old) + "cm") c.cm[i] = k;
      if (n == z + "l" || n == String(old) + "l") c.l[i] = k;
    }
  }
  return c;
}

static String curLogFile, curLogHdr;     // naglowek dzisiejszego pliku (wiersze musza do niego pasowac)

void logRow() {
  if (!sdMounted || !timeValid()) return;
  time_t now = time(nullptr);
  String fn = logPath(now);
  if (fn != curLogFile) {
    curLogFile = fn; curLogHdr = "";
    File r = SD.open(fn);
    if (r) { curLogHdr = r.readStringUntil('\n'); curLogHdr.trim(); r.close(); }
  }
  bool isNew = curLogHdr.length() == 0;
  if (!isNew && curLogHdr != logHeader()) {          // zmieniono liste zbiornikow w ciagu dnia: nowy plik
    fn.replace(".csv", "_" + String((uint32_t)(now % 86400)) + ".csv");
    curLogFile = fn; isNew = true;
  }
  if (isNew) { ensureDir(fn); curLogHdr = logHeader(); }
  File f = SD.open(fn, FILE_APPEND);
  if (!f) { sdMounted = false; logEvent("SD", "Błąd zapisu karty SD"); return; }
  if (isNew) f.println(curLogHdr);
  char d[24]; fmtTime(d, sizeof(d), "%Y-%m-%d;%H:%M:%S", now);
  String row = d;
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    if (!cfg.tank[i].enabled) continue;
    TankState s = tankGet(i);
    row += ";";
    if (s.valid) row += numPL(s.cm, 1) + ";" + numPL(s.pct, 1) + ";" + (s.vol >= 0 ? numPL(s.vol, 0) : String("")) + ";";
    else row += ";;;";
    row += numPL(s.mA, 2) + ";" + stateName(tankEffState(i));
  }
  row += String(";") + (alarmAcked ? "1" : "0");
  f.println(row);
  f.close();
}

// Usuwanie starych plikow: starsze niz retencja albo przy < 10 % wolnego miejsca
void logRotate() {
  if (!sdMounted || !timeValid()) return;
  time_t lim = time(nullptr) - (time_t)cfg.logRetentionDays * 86400;
  char limS[16]; fmtTime(limS, sizeof(limS), "%Y-%m-%d", lim);
  uint32_t removed = 0;
  bool lowSpace = SD.totalBytes() && (SD.totalBytes() - SD.usedBytes()) < SD.totalBytes() / 10;
  File root = SD.open("/logs");
  if (!root) return;
  String oldest;
  for (File y = root.openNextFile(); y; y = root.openNextFile()) {
    if (!y.isDirectory()) continue;
    String yp = String("/logs/") + y.name();
    File yd = SD.open(yp);
    for (File m = yd.openNextFile(); m; m = yd.openNextFile()) {
      if (!m.isDirectory()) continue;
      String mp = yp + "/" + m.name();
      File md = SD.open(mp);
      std::vector<String> del;
      for (File f = md.openNextFile(); f; f = md.openNextFile()) {
        String n = f.name();
        if (n.length() >= 10 && n.substring(0, 10) < String(limS)) del.push_back(mp + "/" + n);
        else if (oldest == "" || mp + "/" + n < oldest) oldest = mp + "/" + n;
        esp_task_wdt_reset();
      }
      md.close();
      for (auto &p : del) { SD.remove(p); removed++; }
    }
    yd.close();
  }
  root.close();
  if (lowSpace && oldest != "" && oldest.indexOf(logPath(time(nullptr))) < 0) { SD.remove(oldest); removed++; }
  if (removed) logEvent("SD", "Usunięto stare pliki logów: " + String(removed));
}

void sdLoop() {      // co 1 s: bez karty proba montowania co 30 s; karta wyjeta wykrywana po bledzie dostepu
  static uint32_t tCheck = 0;
  if (sdMounted && millis() - tCheck > 10000) {
    tCheck = millis();
    if (SD.cardType() == CARD_NONE || !SD.exists("/")) { sdMounted = false; SD.end(); logEvent("SD", "Utracono kartę SD"); }
  }
  if (!sdMounted && millis() - sdLastTry > 30000) {
    sdLastTry = millis();
    if (sdMount()) logEvent("SD", "Karta SD zamontowana (" + String((uint32_t)(SD.cardSize() >> 20)) + " MB)");
  }
}

// Test karty: pin CD, montowanie, typ, pojemnosc, zapis/odczyt
String sdTest(JsonObject r) {
  r["cdPin"] = digitalRead(SD_CD) == LOW ? "LOW (zwarty do GND)" : "HIGH";
  uint32_t t0 = millis();
  bool ok = sdMount();
  r["mountMs"] = millis() - t0;
  r["mounted"] = ok;
  if (!ok) {
    r["result"] = "Nie udało się zamontować karty przy 20/10/4/1 MHz. Sprawdź, czy karta jest dobrze wsunięta (FAT32, do 32 GB) albo spróbuj innej karty.";
    return "";
  }
  static const char *types[] = {"brak", "MMC", "SD", "SDHC/SDXC", "nieznany"};
  uint8_t ct = SD.cardType();
  r["type"] = types[ct < 5 ? ct : 4];
  r["freqMHz"] = sdFreq / 1000000;
  r["cardMB"] = (uint32_t)(SD.cardSize() >> 20);
  r["totalMB"] = (uint32_t)(SD.totalBytes() >> 20);
  r["usedMB"] = (uint32_t)(SD.usedBytes() >> 20);
  String payload = "Monitor App test " + String(millis());
  t0 = millis();
  File f = SD.open("/monitorapp_test.txt", FILE_WRITE);
  bool wr = f && f.print(payload) == payload.length();
  if (f) f.close();
  f = SD.open("/monitorapp_test.txt", FILE_READ);
  String rd = f ? f.readString() : "";
  if (f) f.close();
  SD.remove("/monitorapp_test.txt");
  r["rwMs"] = millis() - t0;
  r["write"] = wr;
  r["read"] = rd == payload;
  r["result"] = (wr && rd == payload) ? "Karta działa poprawnie (zapis i odczyt OK)." : "Karta zamontowana, ale test zapisu/odczytu nie przeszedł (karta zabezpieczona przed zapisem lub uszkodzona).";
  logEvent("SD", String("Test karty: ") + r["result"].as<const char *>());
  return "";
}

// Wypelnienie historii 24 h z plikow logow (po restarcie)
float parsePL(const String &s) { String t = s; t.replace(',', '.'); return t.length() ? t.toFloat() : NAN; }

void histPrefill() {
  if (!sdMounted || !timeValid()) return;
  time_t now = time(nullptr);
  uint32_t nowMin = now / 60;
  curMin = nowMin;
  std::vector<String> v;
  for (int d = 1; d >= 0; d--) {
    File f = SD.open(logPath(now - d * 86400));
    if (!f) continue;
    LogCols c = logCols(f.readStringUntil('\n'));
    uint16_t step = cfg.logIntervalMin ? cfg.logIntervalMin : 1;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      struct tm tm = {};
      if (sscanf(l.c_str(), "%d-%d-%d;%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) continue;
      tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
      uint32_t m = mktime(&tm) / 60;
      if (m >= nowMin || nowMin - m >= HIST_N) continue;
      splitSemi(l, v);
      for (uint8_t i = 0; i < TANK_COUNT; i++) {
        if (c.cm[i] < 0 || c.cm[i] >= (int)v.size()) continue;
        float val = parsePL(v[c.cm[i]]);
        for (uint16_t s2 = 0; s2 < step && m + s2 < nowMin; s2++) histPut(i, m + s2, val);
      }
      esp_task_wdt_reset();
    }
    f.close();
  }
}

void loggerBegin() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdMount();
  sdLastTry = millis();
  Serial.printf("Karta SD: %s\n", sdMounted ? "OK" : "brak");
  histBegin();
}
