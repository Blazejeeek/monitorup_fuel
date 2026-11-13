// MonitorUp - logika alarmu, kasowanie (DI1 / WWW / Modbus / MQTT), wyjscia MOSFET z programowym PWM
#include "outputs.h"
#include <Arduino.h>
#include "hw.h"
#include "config.h"
#include "sensors.h"
#include "logger.h"
#include "stats.h"
#include "net.h"
#include "mqtt.h"

bool alarmAcked = false;
bool alarmActive = false;        // KRYTYCZNY / ZA DUZY / NAGLY SPADEK
bool warnActive = false;         // MINIMALNY / BLAD CZUJNIKA / prognoza
static uint32_t alarmBits = 0;          // aktywne warunki alarmu (nowy bit = ponowne uruchomienie syreny)
uint32_t alarmSince = 0, ackTime = 0;
uint8_t outLevel[8];             // wyliczona jasnosc wyjsc 0-100 %
static uint8_t lastEff[TANK_COUNT] = {255, 255};
volatile bool outTestRun = false;
static bool outputsReady = false;

uint8_t tankEffState(uint8_t i) {
  TankState s = tankGet(i);
  if (s.state == ST_FAULT || s.state == ST_OFF) return s.state;
  if (st[i].drop) return ST_DROP;
  return s.state;
}

uint8_t faultBits() {
  uint8_t f = 0;
  for (uint8_t i = 0; i < TANK_COUNT; i++) if (cfg.tank[i].enabled && tankGet(i).state == ST_FAULT) f |= FAULT_SENSOR;
  if (!adsOk) f |= FAULT_ADS;
  if (!sdMounted) f |= FAULT_SD;
  if (!timeValid()) f |= FAULT_TIME;
  if (!ethHasIp() && !wifiHasIp()) f |= FAULT_NET;
  if (cfg.mqttEn && !mqttConnected()) f |= FAULT_MQTT;
  if (!pcfOk) f |= FAULT_PCF;
  return f;
}

bool isAlarmState(uint8_t s) { return s == ST_CRIT || s == ST_HIGH || s == ST_DROP; }
bool isWarnState(uint8_t s) { return s == ST_MIN || s == ST_FAULT; }

const char *ackSrcName(uint8_t s) { static const char *n[] = {"DI1", "WWW", "Modbus", "MQTT", "API"}; return s < 5 ? n[s] : "?"; }

// 0.4.8: buzer (WY4) wyciszany wejsciem DI1 / strona / Modbus / MQTT - przy alarmie poziomu i przy bledzie czujnika
static bool buzzSilenced = false;

void alarmAck(uint8_t src) {
  const bool sensorF = faultBits() & FAULT_SENSOR;
  if (!alarmActive && !sensorF) return;
  bool changed = false;
  if (alarmActive && !alarmAcked) { alarmAcked = true; changed = true; }   // kasuje tez zatrzasniety NAGLY SPADEK
  if (!buzzSilenced) { buzzSilenced = true; changed = true; }
  if (!changed) return;
  ackTime = millis();
  logEvent("ALARM", String("Wyciszono buzer / skasowano alarm (") + ackSrcName(src) + ")");
}

// ---------------- Programowy PWM (PCF8575, ok. 100 Hz, 10 poziomow) ----------------
static void pwmTask(void *) {
  uint8_t phase = 0;
  for (;;) {
    if (outTestRun || !outputsReady) { vTaskDelay(10); continue; }
    uint8_t mask = 0;
    bool frac = false;
    for (uint8_t k = 0; k < 8; k++) {
      uint8_t lv = (outLevel[k] + 5) / 10;               // 0..10
      if (lv > 0 && lv < 10) frac = true;
      if (phase < lv) mask |= 1 << k;
    }
    if (mask != outState) setOutputs(mask);
    phase = (phase + 1) % 10;
    vTaskDelay(frac ? 1 : 10);
  }
}

// Sprzetowy PWM lampki na wolnym GPIO (opcja)
static uint8_t lampPinAttached = 0;
static void lampHwApply(uint8_t lvl) {
  if (cfg.lampHwGpio != lampPinAttached) {
    if (lampPinAttached) ledcDetach(lampPinAttached);
    lampPinAttached = cfg.lampHwGpio;
    if (lampPinAttached) ledcAttach(lampPinAttached, 2000, 8);
  }
  if (lampPinAttached) ledcWrite(lampPinAttached, (uint32_t)lvl * 255 / 100);
}

void outputsBegin() {
  xTaskCreatePinnedToCore(pwmTask, "pwm", 3072, nullptr, 5, nullptr, 0);
  outputsReady = true;
}

// Test wyjsc na zadanie (nieblokujacy, z pętli)
static uint8_t testIdx = 0; static uint32_t testT = 0;
void outputsTestStart() { outTestRun = true; testIdx = 0; testT = 0; logEvent("WYJŚCIA", "Test wyjść z panelu"); }
static void outputsTestStep() {
  if (millis() - testT < cfg.bootTestMs && testT) return;
  testT = millis();
  while (testIdx < 8 && !(cfg.bootTestMask & (1 << testIdx))) testIdx++;
  if (testIdx >= 8) { setOutputs(0); outTestRun = false; return; }
  setOutputs(1 << testIdx);
  testIdx++;
}

// ---------------- Petla alarmu (co 100 ms) ----------------
void alarmLoop() {
  // DI1: kasowanie na zboczu (filtr 2 odczyty)
  static uint8_t diPrev = 0, diStable = 0;
  uint8_t d1 = diState & 1;
  if (d1 == diPrev) {
    if (d1 && !(diStable & 1) && cfg.ackDi1En) alarmAck(0);
    diStable = d1;
  }
  diPrev = d1;

  uint32_t bits = 0;
  bool warn = false;
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    uint8_t e = tankEffState(i);
    if (e == ST_CRIT) bits |= 1 << (i * 4);
    if (e == ST_HIGH) bits |= 2 << (i * 4);
    if (e == ST_DROP) bits |= 4 << (i * 4);
    if (isWarnState(e) || statForecastWarn(i)) warn = true;
    if (e != lastEff[i]) {
      if (lastEff[i] != 255) {
        logEvent(isAlarmState(e) ? "ALARM" : "STAN", String(cfg.tankName[i]) + ": " + stateName(lastEff[i]) + " → " + stateName(e));
        logRow();
      }
      lastEff[i] = e;
    }
  }
  const uint8_t fb = faultBits();
  const bool sensorFault = fb & FAULT_SENSOR;
  static bool sensorFaultPrev = false;
  if (bits & ~alarmBits) { alarmAcked = false; buzzSilenced = false; if (!alarmBits) alarmSince = time(nullptr); }   // nowy warunek
  if (!bits && alarmBits) { alarmAcked = false; logEvent("ALARM", "Koniec alarmu – stan normalny"); }
  if (sensorFault && !sensorFaultPrev) buzzSilenced = false;                    // nowy blad czujnika - buzer od nowa
  sensorFaultPrev = sensorFault;
  alarmBits = bits;
  alarmActive = bits != 0;
  warnActive = warn;
  if (!alarmActive && !sensorFault) buzzSilenced = false;
  if ((alarmAcked || buzzSilenced) && cfg.reminderMin && millis() - ackTime > (uint32_t)cfg.reminderMin * 60000UL) {
    alarmAcked = false; buzzSilenced = false;
    logEvent("ALARM", "Przypomnienie – alarm nadal trwa");
  }
  statsDropClear(alarmAcked);

  if (outTestRun) { outputsTestStep(); return; }

  // ---- Wyjscia na stale (0.4.8) ----
  // WY1 zielona: poziom OK, system bez uwag | WY2 pomaranczowa: MINIMALNY | WY3 czerwona: KRYTYCZNY / ZA DUZY / NAGLY SPADEK
  // WY4 buzer: alarm WY3 lub blad czujnika, do wyciszenia (DI1) | WY5 przekaznik usterki zbiorczej: zasilony = brak usterki
  // Blad czujnika: WY1-WY3 migaja 1 Hz + buzer. WY6-WY8 nieuzywane.
  const uint8_t e = tankEffState(0);
  const bool fault = fb & cfg.faultMask;
  const bool ph = (millis() % 1000) < 500;
  uint8_t L[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  if (sensorFault) {
    L[0] = L[1] = L[2] = ph ? 100 : 0;
  } else {
    L[0] = (e == ST_NORMAL && !fault) ? 100 : 0;
    L[1] = (e == ST_MIN) ? 100 : 0;
    L[2] = isAlarmState(e) ? 100 : 0;
  }
  L[3] = ((alarmActive || sensorFault) && !buzzSilenced) ? 100 : 0;
  L[4] = fault ? 0 : 100;                         // brak zasilania / usterka = przekaznik puszczony (styki zwarte)
  for (uint8_t k = 0; k < 8; k++) outLevel[k] = L[k];
  lampHwApply(0);
}

bool buzzerSilenced() { return buzzSilenced; }

// Logo przy starcie + test wyjsc OUT1..OUT6 (blokujace, przed startem PWM)
