#include "hw.h"
// MonitorUp - podsumowania hierarchiczne do wykresow wieloletnich (opis w summary.h)
#include "summary.h"
#include <SD.h>
#include <vector>
#include <algorithm>
#include <esp_task_wdt.h>
#include "config.h"
#include "logger.h"
#include "sensors.h"

struct Agg {
  float mn = NAN, mx = NAN, sum = 0, lSum = 0, consCm = 0, consL = 0, refCm = 0, refL = 0;
  uint32_t n = 0, nL = 0;
  bool consOk = false, consLOk = false;
};
struct SumRow { String key; Agg t[TANK_COUNT]; bool full; };

static const char *HDR_TANK = "min_cm;sr_cm;max_cm;sr_l;zuzycie_cm;zuzycie_l;tankowanie_cm;tankowanie_l;probki";

static String monthFile(int y, int m) { char b[32]; snprintf(b, sizeof(b), "/stats/miesiac_%04d-%02d.csv", y, m); return b; }
static String yearFile(int y) { char b[24]; snprintf(b, sizeof(b), "/stats/rok_%04d.csv", y); return b; }
static const char *YEARS_FILE = "/stats/lata.csv";

static String fnum(float v, int d) { return isnan(v) ? String("") : numPL(v, d); }

static void merge(Agg &a, const Agg &b) {
  if (!b.n) return;
  a.mn = isnan(a.mn) ? b.mn : min(a.mn, b.mn);
  a.mx = isnan(a.mx) ? b.mx : max(a.mx, b.mx);
  a.sum += b.sum; a.n += b.n; a.lSum += b.lSum; a.nL += b.nL;
  if (b.consOk) { a.consCm += b.consCm; a.consOk = true; }
  if (b.consLOk) { a.consL += b.consL; a.consLOk = true; }
  a.refCm += b.refCm; a.refL += b.refL;
}

// ---------- pliki podsumowan ----------
static void writeRow(const String &fn, const SumRow &r) {
  bool isNew = !SD.exists(fn);
  if (isNew) ensureDir(fn);
  File f = SD.open(fn, FILE_APPEND);
  if (!f) return;
  if (isNew) {
    String h = "okres";
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      String z = ";z" + String(i + 1) + "_", t = HDR_TANK;
      t.replace(";", z); h += z + t;
    }
    f.println(h + ";pelny");
  }
  String l = r.key;
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    const Agg &a = r.t[i];
    bool L = !isnan(a.lSum) && a.nL;
    l += ";" + fnum(a.n ? a.mn : NAN, 1) + ";" + fnum(a.n ? a.sum / a.n : NAN, 1) + ";" + fnum(a.n ? a.mx : NAN, 1) +
         ";" + fnum(L ? a.lSum / a.nL : NAN, 0) + ";" + fnum(a.consOk ? a.consCm : NAN, 1) + ";" + fnum(a.consLOk ? a.consL : NAN, 0) +
         ";" + fnum(a.n ? a.refCm : NAN, 1) + ";" + fnum(L ? a.refL : NAN, 0) + ";" + String(a.n);
  }
  f.println(l + ";" + (r.full ? "1" : "0"));
  f.close();
}

static std::vector<SumRow> readRows(const String &fn) {
  std::vector<SumRow> out;
  File f = SD.open(fn);
  if (!f) return out;
  f.readStringUntil('\n');
  std::vector<String> v;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (!l.length()) continue;
    splitSemi(l, v);
    if ((int)v.size() < 1 + TANK_COUNT * 9) continue;
    SumRow r; r.key = v[0];
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      Agg &a = r.t[i];
      int b = 1 + i * 9;
      a.n = v[b + 8].toInt();
      float avg = parsePL(v[b + 1]), lAvg = parsePL(v[b + 3]), cc = parsePL(v[b + 4]), cl = parsePL(v[b + 5]);
      a.mn = parsePL(v[b]); a.mx = parsePL(v[b + 2]);
      a.sum = isnan(avg) ? 0 : avg * a.n;
      if (!isnan(lAvg)) { a.lSum = lAvg * a.n; a.nL = a.n; }
      if (!isnan(cc)) { a.consCm = cc; a.consOk = true; }
      if (!isnan(cl)) { a.consL = cl; a.consLOk = true; }
      float rc = parsePL(v[b + 6]), rl = parsePL(v[b + 7]);
      a.refCm = isnan(rc) ? 0 : rc; a.refL = isnan(rl) ? 0 : rl;
      if (!a.n) { a.mn = a.mx = NAN; }
    }
    r.full = (int)v.size() > 1 + TANK_COUNT * 9 && v[1 + TANK_COUNT * 9] == "1";
    out.push_back(r);
    wdtFeed();
  }
  f.close();
  return out;
}

static bool hasKey(const String &fn, const String &key) {
  File f = SD.open(fn);
  if (!f) return false;
  bool found = false;
  while (f.available() && !found) {
    String l = f.readStringUntil('\n');
    if (l.startsWith(key + ";")) found = true;
  }
  f.close();
  return found;
}

// ---------- doba z pliku pomiarow ----------
// Zuzycie = poziom pierwszy - ostatni + tankowania. Tankowanie: wzrost o >= prog wzgledem minimum od ostatniego tankowania.
static bool dayFromFile(const String &path, SumRow &r) {
  File f = SD.open(path);
  if (!f) return false;
  String hdr = f.readStringUntil('\n'); hdr.trim();
  LogCols c = logCols(hdr);
  std::vector<String> v;
  float first[TANK_COUNT], last[TANK_COUNT], base[TANK_COUNT], peak[TANK_COUNT];
  bool refill[TANK_COUNT];
  for (uint8_t i = 0; i < TANK_COUNT; i++) { first[i] = last[i] = base[i] = peak[i] = NAN; refill[i] = false; r.t[i] = Agg(); }
  uint32_t rows = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (!l.length()) continue;
    splitSemi(l, v);
    rows++;
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      if (c.cm[i] < 0 || c.cm[i] >= (int)v.size()) continue;
      float cm = parsePL(v[c.cm[i]]);
      if (isnan(cm)) continue;
      Agg &a = r.t[i];
      a.mn = isnan(a.mn) ? cm : min(a.mn, cm);
      a.mx = isnan(a.mx) ? cm : max(a.mx, cm);
      a.sum += cm; a.n++;
      if (c.l[i] >= 0 && c.l[i] < (int)v.size()) { float lv = parsePL(v[c.l[i]]); if (!isnan(lv)) { a.lSum += lv; a.nL++; } }
      if (isnan(first[i])) first[i] = cm;
      last[i] = cm;
      float th = cfg.tx[i].refillCm > 0 ? cfg.tx[i].refillCm : 2.0f;
      if (isnan(base[i]) || (!refill[i] && cm < base[i])) base[i] = cm;
      if (!refill[i] && cm >= base[i] + th) { refill[i] = true; peak[i] = cm; }
      else if (refill[i]) {
        if (cm > peak[i]) peak[i] = cm;
        else if (cm < peak[i] - th / 2) {
          a.refCm += peak[i] - base[i];
          if (cfg.tx[i].volEn) a.refL += tankVolumeL(i, peak[i]) - tankVolumeL(i, base[i]);
          refill[i] = false; base[i] = cm;
        }
      }
    }
    if ((rows & 63) == 0) wdtFeed();
  }
  f.close();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    Agg &a = r.t[i];
    if (refill[i]) { a.refCm += peak[i] - base[i]; if (cfg.tx[i].volEn) a.refL += tankVolumeL(i, peak[i]) - tankVolumeL(i, base[i]); }
    if (!a.n) continue;
    a.consCm = max(0.0f, first[i] - last[i] + a.refCm); a.consOk = true;
    if (cfg.tx[i].volEn) { a.consL = max(0.0f, tankVolumeL(i, first[i]) - tankVolumeL(i, last[i]) + a.refL); a.consLOk = true; }
  }
  r.full = rows >= 200;     // przy zapisie co 5 min pelna doba ma ok. 288 wierszy
  return rows > 0;
}

static SumRow aggRows(const String &key, const std::vector<SumRow> &rows, bool full) {
  SumRow s; s.key = key; s.full = full;
  for (auto &r : rows) for (uint8_t i = 0; i < TANK_COUNT; i++) merge(s.t[i], r.t[i]);
  return s;
}

// ---------- nadrabianie brakow w tle ----------
static bool kick = true;
static std::vector<String> todoDays;    // RRRR-MM-DD do policzenia
static uint8_t phase = 0;               // 0 lista dni, 1 dni, 2 miesiace, 3 lata, 4 gotowe

void summaryKick() { kick = true; }

static void listDays(const String &today) {
  todoDays.clear();
  File root = SD.open("/logs");
  if (!root) return;
  for (File y = root.openNextFile(); y; y = root.openNextFile()) {
    if (!y.isDirectory()) continue;
    String yp = String("/logs/") + y.name();
    File yd = SD.open(yp);
    for (File m = yd.openNextFile(); m; m = yd.openNextFile()) {
      if (!m.isDirectory()) continue;
      File md = SD.open(yp + "/" + m.name());
      for (File f = md.openNextFile(); f; f = md.openNextFile()) {
        String n = f.name();
        if (n.length() == 14 && n.endsWith(".csv") && n.substring(0, 10) < today) todoDays.push_back(n.substring(0, 10));
        wdtFeed();
      }
      md.close();
    }
    yd.close();
  }
  root.close();
  std::sort(todoDays.begin(), todoDays.end());
}

void summaryLoop() {
  if (!sdMounted || !timeValid()) return;
  char today[12]; fmtTime(today, sizeof(today), "%Y-%m-%d");
  int cy = atoi(today), cm = atoi(today + 5);
  if (kick) { kick = false; phase = 0; }
  switch (phase) {
    case 0: listDays(today); phase = 1; break;
    case 1: {                                            // jeden dzien na wywolanie
      while (!todoDays.empty()) {
        String d = todoDays.front(); todoDays.erase(todoDays.begin());
        int y = d.toInt(), m = d.substring(5, 7).toInt();
        String mf = monthFile(y, m);
        if (hasKey(mf, d)) continue;
        SumRow r; r.key = d;
        if (dayFromFile("/logs/" + d.substring(0, 4) + "/" + d.substring(5, 7) + "/" + d + ".csv", r)) { writeRow(mf, r); Serial.printf("Podsumowanie doby %s -> %s\n", d.c_str(), mf.c_str()); }
        return;
      }
      phase = 2;
      break;
    }
    case 2: {                                            // zakonczone miesiace -> rok
      for (int back = 1; back <= 120; back++) {
        int y = cy, m = cm - back;
        while (m <= 0) { m += 12; y--; }
        String mf = monthFile(y, m);
        if (!SD.exists(mf)) continue;
        char key[8]; snprintf(key, sizeof(key), "%04d-%02d", y, m);
        if (hasKey(yearFile(y), key)) continue;
        auto rows = readRows(mf);
        uint8_t dim = (m == 2) ? ((y % 4 == 0 && (y % 100 || y % 400 == 0)) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
        writeRow(yearFile(y), aggRows(key, rows, rows.size() >= dim));
        Serial.printf("Podsumowanie miesiaca %s -> %s\n", key, yearFile(y).c_str());
        return;
      }
      phase = 3;
      break;
    }
    case 3: {                                            // zakonczone lata -> lata.csv
      for (int y = cy - 1; y >= cy - 10; y--) {
        if (!SD.exists(yearFile(y))) continue;
        char key[6]; snprintf(key, sizeof(key), "%04d", y);
        if (hasKey(YEARS_FILE, key)) continue;
        auto rows = readRows(yearFile(y));
        writeRow(YEARS_FILE, aggRows(key, rows, rows.size() >= 12));
        Serial.printf("Podsumowanie roku %s -> %s\n", key, YEARS_FILE);
        return;
      }
      phase = 4;
      Serial.println("Podsumowania: aktualne");
      break;
    }
    default: break;
  }
}

// ---------- widoki dla strony / API ----------
static SumRow liveToday() {
  SumRow r; r.key = "dziś";
  String p = logPath(time(nullptr));
  if (!dayFromFile(p, r)) r = SumRow();
  r.key = "dziś"; r.full = false;
  return r;
}

static SumRow liveMonth(int y, int m, bool withToday) {
  auto rows = readRows(monthFile(y, m));
  if (withToday) rows.push_back(liveToday());
  char key[8]; snprintf(key, sizeof(key), "%04d-%02d", y, m);
  return aggRows(key, rows, false);
}

static void rowJson(JsonArray rows, const SumRow &r, bool partial) {
  JsonObject o = rows.add<JsonObject>();
  o["k"] = r.key;
  o["full"] = r.full && !partial;
  JsonArray ta = o["t"].to<JsonArray>();
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    const Agg &a = r.t[i];
    JsonObject t = ta.add<JsonObject>();
    auto put = [&](const char *k, float v, int d) { if (isnan(v)) t[k] = nullptr; else t[k] = roundf(v * powf(10, d)) / powf(10, d); };
    put("min", a.n ? a.mn : NAN, 1); put("avg", a.n ? a.sum / a.n : NAN, 1); put("max", a.n ? a.mx : NAN, 1);
    put("lAvg", a.nL ? a.lSum / a.nL : NAN, 0);
    put("cons", a.consOk ? a.consCm : NAN, 1); put("consL", a.consLOk ? a.consL : NAN, 0);
    put("ref", a.n ? a.refCm : NAN, 1); put("refL", a.nL ? a.refL : NAN, 0);
    t["n"] = a.n;
  }
}

String summaryView(const String &view, int y, int m, JsonObject out) {
  if (!sdMounted) return "Brak karty SD";
  if (!timeValid()) return "Czas nieustawiony";
  char today[12]; fmtTime(today, sizeof(today), "%Y-%m-%d");
  int cy = atoi(today), cm = atoi(today + 5);
  JsonArray rows = out["rows"].to<JsonArray>();
  out["catchUp"] = phase < 4;
  if (view == "month") {
    if (y < 2000 || m < 1 || m > 12) return "Podaj rok i miesiąc";
    for (auto &r : readRows(monthFile(y, m))) rowJson(rows, r, false);
    if (y == cy && m == cm) rowJson(rows, liveToday(), true);
  } else if (view == "year") {
    if (y < 2000) return "Podaj rok";
    for (auto &r : readRows(yearFile(y))) rowJson(rows, r, false);
    if (y == cy) rowJson(rows, liveMonth(cy, cm, true), true);
  } else if (view == "years") {
    for (auto &r : readRows(YEARS_FILE)) {
      if (r.key.toInt() < cy - 10) continue;
      rowJson(rows, r, false);
    }
    auto yr = readRows(yearFile(cy));
    yr.push_back(liveMonth(cy, cm, true));
    char key[6]; snprintf(key, sizeof(key), "%04d", cy);
    rowJson(rows, aggRows(key, yr, false), true);
  } else return "Nieznany widok";
  return "";
}
