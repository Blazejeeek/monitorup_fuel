// MonitorUp - statystyki zuzycia, tankowania, prognoza, alarm naglego spadku
#include "stats.h"
#include <SD.h>
#include "summary.h"
// Zuzycie doby = poziom o 00:00 - poziom teraz + suma tankowan (odporne na szum pomiaru).
#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "logger.h"

TankStats st[TANK_COUNT];

static uint32_t ymd(time_t t) { struct tm tm; localtime_r(&t, &tm); return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday; }
static float volOf(uint8_t i, float cm) { return cfg.tx[i].volEn ? tankVolumeL(i, cm) : NAN; }

float statConsToday(uint8_t i, bool litres) {
  TankStats &s = st[i];
  TankState t = tankGet(i);
  if (isnan(s.level00) || !t.valid) return NAN;
  float c = litres ? (volOf(i, s.level00) - volOf(i, t.cm) + s.refL) : (s.level00 - t.cm + s.refCm);
  return c < 0 ? 0 : c;
}

float statAvg(uint8_t i, uint8_t n, bool litres) {
  TankStats &s = st[i];
  float sum = 0; uint8_t cnt = 0;
  for (int k = s.nDays - 1; k >= 0 && cnt < n; k--) {
    if (!s.days[k].full) continue;
    float v = litres ? s.days[k].consL : s.days[k].consCm;
    if (isnan(v)) continue;
    sum += v; cnt++;
  }
  return cnt >= 3 || (n <= 1 && cnt) ? sum / cnt : NAN;
}

float statYesterday(uint8_t i, bool litres) {
  TankStats &s = st[i];
  if (!s.nDays) return NAN;
  return litres ? s.days[s.nDays - 1].consL : s.days[s.nDays - 1].consCm;
}

// Prognoza "wystarczy na X dni" (NAN = brak danych)
float statForecast(uint8_t i) {
  return NAN;                    // prognoza „wystarczy na” usunieta (0.4.4); Modbus zwraca „brak danych”
  TankState t = tankGet(i);
  if (!t.valid || !cfg.tx[i].statsEn) return NAN;
  const TankCal &c = cfg.tank[i];
  float ref = cfg.tx[i].fcRef == 0 ? c.thrMin : cfg.tx[i].fcRef == 1 ? c.thrCrit : 0;
  bool L = cfg.tx[i].volEn;
  float avg = statAvg(i, 7, L);
  if (isnan(avg) || avg < 0.01f) return NAN;
  float left = L ? (tankVolumeL(i, t.cm) - tankVolumeL(i, ref)) : (t.cm - ref);
  if (left <= 0) return 0;
  return left / avg;
}

bool statForecastWarn(uint8_t i) {
  return false;                  // ostrzezenie prognozy usuniete (0.4.4)
  float f = statForecast(i);
  return cfg.tx[i].statsEn && cfg.tx[i].fcWarnDays && !isnan(f) && f < cfg.tx[i].fcWarnDays;
}

static void pushDay(TankStats &s, const DayRec &d) {
  if (s.nDays == STAT_DAYS) { memmove(s.days, s.days + 1, sizeof(DayRec) * (STAT_DAYS - 1)); s.nDays--; }
  s.days[s.nDays++] = d;
}

// ---------------- zapis/odczyt na SD ----------------
static void statsSaveToday() {
  if (!sdMounted) return;
  File f = SD.open("/stats/today.txt", FILE_WRITE);
  if (!f) { ensureDir("/stats/today.txt"); f = SD.open("/stats/today.txt", FILE_WRITE); }
  if (!f) return;
  for (uint8_t i = 0; i < TANK_COUNT; i++)
    f.printf("%u %.2f %u %.2f %.1f %.2f %.1f %u\n", st[i].day, st[i].level00, st[i].full, st[i].refCm, st[i].refL,
             st[i].lastRefCm, st[i].lastRefL, st[i].lastRefT);
  f.close();
}

static void statsWriteDay(uint32_t day) {
  if (!sdMounted) return;
  char fn[24]; snprintf(fn, sizeof(fn), "/stats/%u.csv", day / 10000);
  bool isNew = !SD.exists(fn);
  if (isNew) ensureDir(fn);
  File f = SD.open(fn, FILE_APPEND);
  if (!f) return;
  if (isNew) f.println("data;z1_zuzycie_cm;z1_zuzycie_l;z1_tankowanie_cm;z1_tankowanie_l;z1_pelna_doba;z2_zuzycie_cm;z2_zuzycie_l;z2_tankowanie_cm;z2_tankowanie_l;z2_pelna_doba");
  String r = String(day / 10000) + "-" + (day / 100 % 100 < 10 ? "0" : "") + String(day / 100 % 100) + "-" + (day % 100 < 10 ? "0" : "") + String(day % 100);
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    const DayRec &d = st[i].days[st[i].nDays - 1];
    r += ";" + (isnan(d.consCm) ? String("") : numPL(d.consCm, 1)) + ";" + (isnan(d.consL) ? String("") : numPL(d.consL, 0)) +
         ";" + numPL(d.refCm, 1) + ";" + (isnan(d.refL) ? String("") : numPL(d.refL, 0)) + ";" + String(d.full);
  }
  f.println(r);
  f.close();
}

static void statsLoadHistory() {
  if (!sdMounted || !timeValid()) return;
  uint32_t today = ymd(time(nullptr));
  for (int yOff = 1; yOff >= 0; yOff--) {
    char fn[24]; snprintf(fn, sizeof(fn), "/stats/%u.csv", today / 10000 - yOff);
    File f = SD.open(fn);
    if (!f) continue;
    f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n');
      int y, m, d;
      if (sscanf(l.c_str(), "%d-%d-%d", &y, &m, &d) != 3) continue;
      uint32_t day = y * 10000 + m * 100 + d;
      if (day >= today) continue;
      std::vector<String> v; int pos = 0, q;
      while ((q = l.indexOf(';', pos)) >= 0) { v.push_back(l.substring(pos, q)); pos = q + 1; }
      v.push_back(l.substring(pos));
      if (v.size() < 11) continue;
      for (uint8_t i = 0; i < TANK_COUNT; i++) {
        DayRec r = {day, parsePL(v[1 + i * 5]), parsePL(v[2 + i * 5]), parsePL(v[3 + i * 5]), parsePL(v[4 + i * 5]), (uint8_t)v[5 + i * 5].toInt()};
        if (isnan(r.refCm)) r.refCm = 0;
        pushDay(st[i], r);
      }
    }
    f.close();
  }
  File f = SD.open("/stats/today.txt");
  if (f) {
    for (uint8_t i = 0; i < TANK_COUNT && f.available(); i++) {
      String l = f.readStringUntil('\n');
      unsigned day, full, lrt; float l00, rc, rl, lrc, lrl;
      if (sscanf(l.c_str(), "%u %f %u %f %f %f %f %u", &day, &l00, &full, &rc, &rl, &lrc, &lrl, &lrt) == 8) {
        st[i].lastRefCm = lrc; st[i].lastRefL = lrl; st[i].lastRefT = lrt;
        if (day == today) { st[i].day = day; st[i].level00 = l00; st[i].full = full; st[i].refCm = rc; st[i].refL = rl; }
      }
    }
    f.close();
  }
}

void statsBegin() {
  for (auto &s : st) { memset(&s, 0, sizeof(s)); s.level00 = NAN; }
  statsLoadHistory();
}

// ---------------- co minute ----------------
static float histMinMax(uint8_t i, uint32_t from, uint32_t to, bool wantMax) {
  float r = NAN;
  for (uint32_t m = from; m <= to; m++) {
    float v = histGet(i, m);
    if (isnan(v)) continue;
    if (isnan(r) || (wantMax ? v > r : v < r)) r = v;
  }
  return r;
}

void statsMinute(uint32_t m) {        // m = zamknieta minuta (epoch/60)
  time_t now = time(nullptr);
  uint32_t today = ymd(now);
  struct tm tm; localtime_r(&now, &tm);

  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    TankStats &s = st[i];
    const TankExtra &x = cfg.tx[i];
    float cur = histGet(i, m);

    // zmiana doby
    if (s.day != today) {
      if (s.day) {
        DayRec d = {s.day, NAN, NAN, s.refCm, x.volEn ? s.refL : NAN, s.full};
        if (!isnan(s.level00) && !isnan(cur)) {
          d.consCm = max(0.0f, s.level00 - cur + s.refCm);
          if (x.volEn) d.consL = max(0.0f, volOf(i, s.level00) - volOf(i, cur) + s.refL);
        }
        pushDay(s, d);
      }
      bool hadDay = s.day != 0;
      s.day = today; s.level00 = cur; s.full = hadDay; s.refCm = 0; s.refL = 0;
      if (i == TANK_COUNT - 1 && hadDay) { statsWriteDay(st[0].days[st[0].nDays - 1].day); summaryKick(); }
    }
    if (isnan(s.level00) && !isnan(cur)) s.level00 = cur;
    if (isnan(cur) || !cfg.tank[i].enabled) continue;

    // tankowanie: wzrost >= refillCm wzgledem minimum z 15 min
    float mn15 = histMinMax(i, m - 15, m, false);
    if (!s.refilling && x.statsEn && !isnan(mn15) && cur - mn15 >= x.refillCm) {
      s.refilling = true; s.refBase = mn15; s.refPeak = cur; s.refPeakMin = m;
    }
    if (s.refilling) {
      if (cur > s.refPeak + 0.2f) { s.refPeak = cur; s.refPeakMin = m; }
      if (m - s.refPeakMin >= 15) {                         // 15 min bez dalszego wzrostu = koniec
        s.refilling = false;
        float dc = s.refPeak - s.refBase;
        float dl = x.volEn ? volOf(i, s.refPeak) - volOf(i, s.refBase) : NAN;
        s.refCm += dc; if (x.volEn) s.refL += dl;
        s.lastRefCm = dc; s.lastRefL = x.volEn ? dl : 0; s.lastRefT = now;
        logEvent("TANKOWANIE", String(cfg.tankName[i]) + ": +" + numPL(dc, 1) + " cm" + (x.volEn ? " (+" + numPL(dl, 0) + " l)" : ""));
      }
    }

    // nagly spadek
    if (!x.dropEn || s.refilling || now < s.suspendUntil) continue;
    float mxT = histMinMax(i, m - x.dropMin, m, true);
    bool hit = !isnan(mxT) && mxT - cur >= x.dropCm;
    float amount = hit ? mxT - cur : 0;
    if (!hit && x.nightEn) {
      bool night = x.nightFrom > x.nightTo ? (tm.tm_hour >= x.nightFrom || tm.tm_hour < x.nightTo)
                                           : (tm.tm_hour >= x.nightFrom && tm.tm_hour < x.nightTo);
      if (night) {
        float mx60 = histMinMax(i, m - 60, m, true);
        if (!isnan(mx60) && mx60 - cur >= x.nightCm) { hit = true; amount = mx60 - cur; }
      }
    }
    if (hit) {
      s.dropLastMin = m;
      if (!s.drop) {
        s.drop = true; s.dropT = now; s.dropAmount = amount;
        logEvent("ALARM", String(cfg.tankName[i]) + ": NAGŁY SPADEK o " + numPL(amount, 1) + " cm");
      } else if (amount > s.dropAmount) s.dropAmount = amount;
    }
  }
  if (cfg.logIntervalMin && (m % cfg.logIntervalMin) == 0) statsSaveToday();
}

// Wygaszenie alarmu naglego spadku: po skwitowaniu i 30 min bez dalszego spadku
void statsDropClear(bool acked) {
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    TankStats &s = st[i];
    if (s.drop && acked && curMin - s.dropLastMin >= 30) {
      s.drop = false;
      logEvent("ALARM", String(cfg.tankName[i]) + ": koniec alarmu nagłego spadku");
    }
    if (s.drop && !cfg.tx[i].dropEn) s.drop = false;
  }
}

void statsSuspend(uint8_t i, uint16_t minutes) {
  st[i].suspendUntil = time(nullptr) + minutes * 60;
  st[i].drop = false;
  logEvent("ALARM", String(cfg.tankName[i]) + ": wstrzymano alarm nagłego spadku na " + String(minutes) + " min");
}
