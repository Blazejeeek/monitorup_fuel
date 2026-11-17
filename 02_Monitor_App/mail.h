#pragma once
// Powiadomienia e-mail (0.5.6): SMTP (SSL/TLS, STARTTLS albo bez szyfrowania) we wlasnym zadaniu FreeRTOS.
// Zdarzenia z petli glownej trafiaja do kolejki - wysylka nie blokuje alarmow, strony ani Modbus.
#include <Arduino.h>

void mailBegin();
void mailLoop();                                       // petla glowna: wpisy do dziennika zdarzen, raport dzienny
void mailEvent(uint16_t kind, uint8_t tank, const String &detail);   // kind = MailKind (config.h)
bool mailTest(const String &to);                       // wiadomosc testowa (false = kolejka pelna / wylaczone)
String mailStatusText();                               // ostatni wynik wysylki - do strony
