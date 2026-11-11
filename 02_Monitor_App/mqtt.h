// Monitor App - MQTT + autodiscovery Home Assistant
#pragma once
#include <Arduino.h>

bool mqttConnected();
void mqttApply();                  // po zmianie ustawien
void mqttLoop();
