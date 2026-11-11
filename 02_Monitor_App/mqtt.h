// MonitorUp - MQTT
#pragma once
#include <Arduino.h>

bool mqttConnected();
String mqttStatusText();          // stan dla strony, np. "brak połączenia (prób: 3), następna próba za 40 s"
void mqttBegin();                  // start zadania MQTT (rdzen 0)
void mqttApply();                  // po zmianie ustawien
void mqttLoop();                   // petla glowna: polecenia z MQTT i dziennik
