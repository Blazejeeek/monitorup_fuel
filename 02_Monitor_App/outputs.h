// MonitorUp - logika alarmu, kasowanie (DI1 / WWW / Modbus / MQTT / API), wyjscia MOSFET z programowym PWM
#pragma once
#include <Arduino.h>

extern bool alarmAcked;
extern bool alarmActive;           // KRYTYCZNY / ZA DUZY / NAGLY SPADEK
extern bool warnActive;            // MINIMALNY / BLAD CZUJNIKA / prognoza
extern uint32_t alarmSince;
extern uint8_t outLevel[8];        // jasnosc wyjsc 0-100 %
extern volatile bool outTestRun;

uint8_t tankEffState(uint8_t i);   // stan z poziomu + nagly spadek
uint8_t faultBits();               // aktywne przyczyny usterki (FAULT_*), bez maski
bool isAlarmState(uint8_t s);
bool isWarnState(uint8_t s);
void alarmAck(uint8_t src);        // 0 DI1, 1 WWW, 2 Modbus, 3 MQTT, 4 API
void outputsBegin();
void outputsTestStart();
void alarmLoop();                  // co 100 ms
