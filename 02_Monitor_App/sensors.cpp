// Monitor App - pomiar przetwornikow: probki co 100 ms, srednia z okna 2 s (odrzucone skrajne), stany alarmowe
#include "sensors.h"
#include <Arduino.h>
#include "hw.h"
#include "config.h"

const char *stateName(uint8_t s) {
  switch (s) {
    case ST_NORMAL: return "NORMALNY";
    case ST_MIN:    return "MINIMALNY";
    case ST_CRIT:   return "KRYTYCZNY";
    case ST_HIGH:   return "ZA DUŻY";
    case ST_FAULT:  return "BŁĄD CZUJNIKA";
    case ST_DROP:   return "NAGŁY SPADEK";
    default:        return "WYŁĄCZONY";
  }
}

#define SAMPLE_MS   100
#define WINDOW_N    20            // 20 x 100 ms = 2 s

static TankState tankState[TANK_COUNT];
static portMUX_TYPE tankMux = portMUX_INITIALIZER_UNLOCKED;
bool adsOk = false;

TankState tankGet(uint8_t i) {
  portENTER_CRITICAL(&tankMux);
  TankState s = tankState[i];
  portEXIT_CRITICAL(&tankMux);
  return s;
}

// ---------------- stan wewnetrzny zadania pomiarowego ----------------
static int16_t  ring[TANK_COUNT][WINDOW_N];
static uint8_t  ringCnt[TANK_COUNT], ringPos[TANK_COUNT];
static float    ema[TANK_COUNT];
static bool     emaInit[TANK_COUNT];
static uint8_t  pendState[TANK_COUNT];
static uint32_t pendSince[TANK_COUNT];
static bool     stateInit[TANK_COUNT];

// ---------------- Objetosc ----------------
float tankVolumeL(uint8_t i, float cm) {
  const TankExtra &x = cfg.tx[i];
  if (cm < 0) cm = 0;
  switch (x.shape) {
    case SH_HCYL: {                               // walec lezacy: srednica dimA, dlugosc dimB
      float r = x.dimA / 2, h = min(cm, x.dimA);
      float a = r * r * acosf((r - h) / r) - (r - h) * sqrtf(fmaxf(0, 2 * r * h - h * h));
      return a * x.dimB / 1000.0f;
    }
    case SH_VCYL: return (float)M_PI * (x.dimA / 2) * (x.dimA / 2) * cm / 1000.0f;
    case SH_BOX:  return x.dimA * x.dimB * cm / 1000.0f;
    default: {                                    // tabela cm -> litry (interpolacja liniowa)
      if (x.tblN < 2) return 0;
      if (cm <= x.tblCm[0]) return x.tblL[0];
      for (uint8_t k = 1; k < x.tblN; k++)
        if (cm <= x.tblCm[k]) {
          float d = x.tblCm[k] - x.tblCm[k - 1];
          return d > 0 ? x.tblL[k - 1] + (cm - x.tblCm[k - 1]) * (x.tblL[k] - x.tblL[k - 1]) / d : x.tblL[k];
        }
      return x.tblL[x.tblN - 1];
    }
  }
}

float tankTotalL(uint8_t i) {
  const TankExtra &x = cfg.tx[i];
  if (x.shape == SH_HCYL) return tankVolumeL(i, x.dimA);
  if (x.shape == SH_TABLE) return x.tblN ? x.tblL[x.tblN - 1] : 0;
  return tankVolumeL(i, cfg.tank[i].heightCm);
}

static float trimmedMean(const int16_t *v, uint8_t n) {
  int16_t s[WINDOW_N];
  memcpy(s, v, n * sizeof(int16_t));
  for (uint8_t i = 1; i < n; i++) {               // sortowanie przez wstawianie (n <= 20)
    int16_t x = s[i]; int8_t j = i - 1;
    while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
    s[j + 1] = x;
  }
  uint8_t cut = n >= 10 ? 2 : 0;                  // odrzuc 2 najmniejsze i 2 najwieksze
  int32_t sum = 0;
  for (uint8_t i = cut; i < n - cut; i++) sum += s[i];
  return (float)sum / (n - 2 * cut);
}

static uint8_t evalState(uint8_t i, const TankCal &c, float cm, uint8_t cur) {
  float critB = (cur == ST_CRIT) ? c.thrCrit + c.hyst : c.thrCrit;
  float minB  = (cur == ST_MIN || cur == ST_CRIT) ? c.thrMin + c.hyst : c.thrMin;
  float highB = (cur == ST_HIGH) ? c.thrHigh - c.hyst : c.thrHigh;
  if (cm <= critB) return ST_CRIT;
  if (cm <= minB)  return ST_MIN;
  if (cm >= highB) return ST_HIGH;
  return ST_NORMAL;
}

static void computeTank(uint8_t i) {
  const TankCal &c = cfg.tank[i];
  TankState s = tankGet(i);
  s.updated = millis();
  if (!c.enabled) {
    s.valid = false; s.state = ST_OFF; stateInit[i] = false;
  } else if (!adsOk || ringCnt[i] == 0) {
    s.valid = false; s.state = ST_FAULT;
  } else {
    float raw = trimmedMean(ring[i], ringCnt[i]);
    if (c.smooth > 0) {
      float a = 1.0f - (c.smooth / 100.0f) * 0.95f;
      if (!emaInit[i]) { ema[i] = raw; emaInit[i] = true; }
      ema[i] += a * (raw - ema[i]);
      raw = ema[i];
    } else emaInit[i] = false;

    s.volt = raw * ADS_FS_V / 32768.0f;
    s.mA = c.shuntOhm > 1 ? s.volt * 1000.0f / c.shuntOhm : 0;
    float x = c.type == 0 ? s.mA : s.volt;
    bool fault = c.type == 0 ? (s.mA < 3.6f || s.mA > 21.0f) : (s.volt < -0.1f || s.volt > 5.3f);
    float span = c.maxX - c.minX;
    if (fabsf(span) < 1e-6f) fault = true;

    if (fault) {
      s.valid = false; s.state = ST_FAULT; stateInit[i] = false;
    } else {
      float cm = c.minCm + (x - c.minX) * (c.maxCm - c.minCm) / span + c.offsetCm;
      if (cm < 0) cm = 0;
      s.cm = cm;
      s.pct = c.heightCm > 0 ? constrain(cm / c.heightCm * 100.0f, 0.0f, 100.0f) : 0;
      if (cfg.tx[i].volEn) {
        s.vol = tankVolumeL(i, cm);
        s.volTotal = tankTotalL(i);
        if (s.volTotal > 0) s.pct = constrain(s.vol / s.volTotal * 100.0f, 0.0f, 100.0f);
      } else { s.vol = -1; s.volTotal = 0; }
      s.valid = true;
      uint8_t cur = stateInit[i] ? s.state : ST_NORMAL;
      uint8_t cand = evalState(i, c, cm, cur);
      if (!stateInit[i]) {                       // pierwszy wazny pomiar: stan od razu
        s.state = cand; pendState[i] = cand; stateInit[i] = true;
      } else if (cand != s.state) {
        if (pendState[i] != cand) { pendState[i] = cand; pendSince[i] = millis(); }
        if (millis() - pendSince[i] >= (uint32_t)c.delayS * 1000UL) s.state = cand;
      } else pendState[i] = s.state;
    }
  }
  portENTER_CRITICAL(&tankMux);
  tankState[i] = s;
  portEXIT_CRITICAL(&tankMux);
}

static void sensorTask(void *) {
  TickType_t last = xTaskGetTickCount();
  uint8_t tick = 0;
  for (;;) {
    bool allOk = true;
    for (uint8_t i = 0; i < TANK_COUNT; i++) {
      if (!cfg.tank[i].enabled) { ringCnt[i] = 0; continue; }
      bool ok;
      int16_t v = adsReadChannel(cfg.tank[i].channel & 3, ok);
      if (!ok) { allOk = false; continue; }
      ring[i][ringPos[i]] = v;
      ringPos[i] = (ringPos[i] + 1) % WINDOW_N;
      if (ringCnt[i] < WINDOW_N) ringCnt[i]++;
      portENTER_CRITICAL(&tankMux);
      tankState[i].raw = v;
      portEXIT_CRITICAL(&tankMux);
    }
    adsOk = allOk;
    if (++tick >= 1000 / SAMPLE_MS) {           // wynik co 1 s z okna 2 s
      tick = 0;
      for (uint8_t i = 0; i < TANK_COUNT; i++) computeTank(i);
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

// Po zmianie wejscia/kalibracji: wyczysc okno
void sensorsReset() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) { ringCnt[i] = 0; ringPos[i] = 0; emaInit[i] = false; stateInit[i] = false; }
}

void sensorsBegin() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) { tankState[i] = {}; tankState[i].state = ST_FAULT; }
  xTaskCreatePinnedToCore(sensorTask, "sensors", 4096, nullptr, 3, nullptr, 1);
}
