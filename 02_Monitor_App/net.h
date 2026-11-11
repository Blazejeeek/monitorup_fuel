// Monitor App - siec: Ethernet W5500, WiFi klient, Access Point, mDNS
#pragma once
#include <Arduino.h>

#define MDNS_NAME "monitor"

extern bool ethStarted;
extern bool apOn;

bool ethHasIp();
bool wifiHasIp();
void netBegin();
void netLoop();                    // co 1 s
