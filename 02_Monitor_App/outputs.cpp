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

void alarmAck(uint8_t src) {
  if (cfg.ackAuto || !alarmActive || alarmAcked) return;   // tryb automatyczny: bez kasowania
  alarmAcked = true;
  ackTime = millis();
  logEvent("ALARM", String("Skasowano alarm (") + ackSrcName(src) + ")");
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
  if (bits & ~alarmBits) { alarmAcked = false; if (!alarmBits) alarmSince = time(nullptr); }   // nowy warunek
  if (!bits && alarmBits) { alarmAcked = false; logEvent("ALARM", "Koniec alarmu – stan normalny"); }
  alarmBits = bits;
  alarmActive = bits != 0;
  warnActive = warn;
  if (alarmAcked && cfg.reminderMin && millis() - ackTime > (uint32_t)cfg.reminderMin * 60000UL) {
    alarmAcked = false;
    logEvent("ALARM", "Przypomnienie – alarm nadal trwa");
  }
  if (cfg.ackAuto) alarmAcked = false;                  // tryb automatyczny: alarm trwa do powrotu do normy
  statsDropClear(alarmAcked);

  if (outTestRun) { outputsTestStep(); return; }

  bool blinkPh;
  const bool sensorFault = cfg.faultBlink && (faultBits() & FAULT_SENSOR);
  uint8_t lampLvl = 0;
  for (uint8_t k = 0; k < 8; k++) {
    const OutCfg &o = cfg.out[k];
    uint8_t v = 0;
    bool forceBlink = false;
    uint8_t pre = o.pwmPre, post = o.pwmPost;
    switch (o.func) {
      case OF_MANUAL: v = o.manual; break;
      case OF_SIREN:  if (alarmActive) v = alarmAcked ? post : pre; break;
      case OF_LAMP:
        if (alarmActive) v = alarmAcked ? post : pre;
        else if (warnActive && cfg.lampOnMin) { v = post ? post : 20; forceBlink = true; }
        break;
      case OF_COMMON: if (alarmActive) v = alarmAcked ? post : pre; else if (warnActive) v = pre; break;
      case OF_TANK1: case OF_TANK2: {
        uint8_t e = tankEffState(o.func - OF_TANK1);
        if (isAlarmState(e)) v = alarmAcked ? post : pre; else if (isWarnState(e)) v = pre;
        break;
      }
      case OF_WARN: if (warnActive) v = pre; break;
      case OF_MIN: case OF_MAX: case OF_CRIT: {
        const uint8_t want = o.func == OF_MIN ? ST_MIN : o.func == OF_MAX ? ST_HIGH : ST_CRIT;
        for (uint8_t i = 0; i < TANK_COUNT; i++) {
          if (!cfg.tank[i].enabled || tankEffState(i) != want) continue;
          v = (o.func == OF_MIN) ? pre : (alarmAcked ? post : pre);   // MIN = ostrzezenie (bez kasowania)
        }
        break;
      }
      case OF_FAULT: if (faultBits() & cfg.faultMask) v = pre; break;   // usterka zbiorcza wg Ustawienia → Wyjścia
      default: break;
    }
    if (v && (o.blink || forceBlink)) {
      uint32_t onT = forceBlink ? 1000 : o.onMs, offT = forceBlink ? 1000 : o.offMs;
      blinkPh = (millis() % (onT + offT)) < onT;
      if (!blinkPh) v = 0;
    }
    if (sensorFault && k < 3) {                       // blad czujnika: WY1-WY3 migaja 1 Hz
      v = (millis() % 1000) < 500 ? (o.pwmPre ? o.pwmPre : 100) : 0;
    }
    outLevel[k] = v;
    if (o.func == OF_LAMP && v > lampLvl) lampLvl = v;
  }
  lampHwApply(lampLvl);
}

// Logo przy starcie + test wyjsc OUT1..OUT6 (blokujace, przed startem PWM)
