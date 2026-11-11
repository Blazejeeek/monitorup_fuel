// MonitorUp - serwer WWW: logowanie, tokeny API, API JSON /api/v1, strony
#pragma once
#include <Arduino.h>

void scheduleReboot(uint32_t ms = 1500);
void webBegin();
void webLoop();
