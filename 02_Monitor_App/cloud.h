// MonitorUp - polaczenie z chmura Zdalio (0.6.0, karta #179). Zastepuje MQTT dla BMS z wersji <= 0.5.x.
//
// Jeden przelacznik cfg.cloudEn: wlaczony = wysylka danych i polecenia z chmury, wylaczony = brak polaczen.
// MQTT przez TLS do brokera Zdalio (adres i haslo z danych fabrycznych), login = hw_id (11 znakow z MAC),
// tematy e-tank/<hw_id>/v1/..., protokol Zdalio v1 (koperta, zdarzenia z ack, polecenia z cid/exp, cfg retained).
// Polecenia tylko w granicach strony sterownika: sys/info, sys/log, tank/ack, sys/reboot, sys/ota, sys/rotate, cfg.
// Zadanie FreeRTOS na rdzeniu 0; wszystko, co dotyka karty SD i ustawien, wykonuje petla glowna (cloudLoop).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum CloudSt : uint8_t { CS_OFF, CS_NOID, CS_NONET, CS_CONN, CS_OK, CS_NOACK, CS_OTA, CS_DNS, CS_TCP, CS_TLS, CS_AUTH };

void        cloudBootCheck();               // setup(), po fabLoad(): potwierdzenie / wykrycie cofniecia po OTA z chmury
void        cloudBegin();                   // setup(): start zadania
void        cloudApply();                   // po zmianie cfg.cloudEn (petla glowna)
void        cloudTest(uint16_t s);          // PROV-CLOUDTEST: chmura wlaczona tylko w RAM na s sekund (0 = koniec), ustawienia bez zmian
void        cloudLocalOta();                // po udanym OTA ze strony (kasuje probe wersji z chmury)
void        cloudLoop();                    // petla glowna: polecenia z chmury, dziennik, ustawienia
void        cloudOnLog(const char *type, const String &msg);   // hak z logEvent(): wpis dziennika -> zdarzenie evt/*
bool        cloudConnected();               // polaczono (takze bez potwierdzen zdarzen)
bool        cloudFault();                   // usterka zbiorcza: chmura wlaczona, brak polaczenia dluzej niz cfg.cloudFaultMin
uint8_t     cloudState();
const char *cloudStCode();                  // off, noid, nonet, conn, ok, noack, ota, dns, tcp, tls, auth
String      cloudStatusText();              // opis stanu po polsku (strona, dziennik)
String      cloudShortText();               // krotki opis na OLED (do 21 znakow)
String      cloudHwId();                    // 11 znakow z MAC
String      cloudHwLabel();                 // XXXX-XXXX-XXX
int         cloudOtaPct();                  // postep aktualizacji z chmury, -1 = brak
bool        cloudOtaBusy();                 // trwa aktualizacja z chmury (strona odrzuca wtedy OTA lokalne)
void        cloudStatusJson(JsonObject o);  // /api/v1/status -> cloud{...}
