// MonitorUp - dane fabryczne egzemplarza (0.5.6, karta #177)
//
// Zapis raz na stanowisku produkcyjnym przez USB (tools/produkcja.py w repo rejestr-produkcji), osobna przestrzen NVS "fab":
//  - klucz "d"   FabData: numer seryjny, wersja sprzetu, data produkcji, haslo Wi-Fi (AP), skrot hasla startowego panelu,
//                dane chmury MonitorUp.pl (login = numer seryjny, haslo, adres) - chmura jeszcze nieuzywana,
//  - klucz "cal" fabryczna tabela kalibracji pradu wejscia A4 (CalTable) z testu na stanowisku,
//  - klucze "crt" / "key" zarezerwowane na certyfikat urzadzenia (mTLS) - nieuzywane.
// Ustawienia fabryczne, reset DOWNLOAD, kopia/odtwarzanie ustawien i aktualizacja programu NIE kasuja przestrzeni "fab".
// Kopia numeru seryjnego z CRC w EEPROM 24C02 (adres EE_SN_BASE) - numer zostaje po wyczyszczeniu calego flash.
//
// Polecenia na porcie szeregowym USB (jedna linia, odpowiedz zaczyna sie od "PROV-"); jawnych hasel nie wypisujemy nigdy:
//   PROV-INFO                 MAC, wersje, numer, stan danych fabrycznych, sumy kontrolne hasel (8 zn. SHA-256)
//   PROVISION {json}          sn, hw, date, ap_pass, panel_pass, cloud_pass, cloud_host, cloud_port
//                             ponownie tylko z "serwis":true i "sn_stary" = obecny numer
//   PROV-CAL {json}           fabryczna kalibracja wejscia A4 (format pliku kalibracji) - raz, potem tylko z "serwis":true i "sn"
//   PROV-MEAS                 pomiar A4 (raw, mA), wejscia DI, wyjscia - do testu na stanowisku
//   PROV-OUTTEST              test wyjsc OUT1..OUT8 po kolei (jak z panelu)
#pragma once
#include <Arduino.h>
#include "calib.h"

#define FAB_SN_LEN 16

struct FabData {
  uint16_t magic;               // 'FB'
  uint8_t  ver;
  char     sn[FAB_SN_LEN];      // MU-RRMM-NNNN
  char     hw[8];               // wersja sprzetu, np. "1.0"
  char     date[11];            // data produkcji RRRR-MM-DD
  char     apPass[33];          // haslo sieci Wi-Fi sterownika (AP), jawne - potrzebne do WPA2
  uint8_t  panelSalt[16];       // haslo startowe panelu: tylko skrot SHA-256(sol + haslo)
  uint8_t  panelHash[32];
  char     cloudPass[33];       // haslo chmury MonitorUp.pl (login = numer seryjny)
  char     cloudHost[64];
  uint16_t cloudPort;
  uint8_t  chkPanel[4];         // pierwsze bajty SHA-256(haslo panelu) - do odczytu kontrolnego na stanowisku
  uint32_t programmed;          // czas zapisu (epoch, 0 = nieznany)
  uint8_t  reserved[32];
};

enum FabState : uint8_t { FAB_NONE = 0, FAB_OK = 1, FAB_EE_ONLY = 2 };   // brak / dane fabryczne / tylko numer z kopii EEPROM

extern FabData fab;
extern FabState fabState;

void        fabLoad();                        // przy starcie, PRZED cfgLoad()
bool        fabProgrammed();
const char *fabSn();                          // numer seryjny ("" = brak)
bool        fabCal(CalTable &out);            // fabryczna tabela kalibracji pradu (false = brak)
bool        fabIsStartPanelPass(const char *pw); // czy to haslo startowe z naklejki (zakaz ponownego ustawienia)
void        fabLoop();                        // polecenia PROV-* z portu szeregowego (petla glowna)
