// MonitorUp - ustawienia <-> JSON (strona Ustawienia, kopia ustawien, import)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

String ip2s(uint32_t v);
bool s2ip(const char *s, uint32_t &out);
// secrets = true: hasla WiFi/AP/MQTT w wyniku; backup = true: znacznik pliku kopii
void cfgToJson(JsonObject r, bool secrets, bool backup = false);
// Zapis czesciowy (tylko podane klucze); "" = OK, inaczej opis bledu (nic nie zmieniono)
String cfgFromJson(JsonObjectConst r);
