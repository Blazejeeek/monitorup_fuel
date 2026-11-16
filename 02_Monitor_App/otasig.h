// MonitorUp - weryfikacja podpisu plikow aktualizacji (0.5.4)
// Plik podpisany = obraz programu + 72 bajty: podpis ECDSA P-256 (r||s, 64 B) nad SHA-256 obrazu + "MUPSIG01".
// Podpis dokleja narzedzie sign_fw.py (klucz prywatny tylko na serwerze budowania). Dodatkowe bajty za obrazem
// trafiaja do partycji, ale nie przeszkadzaja - dlugosc programu wynika z jego naglowka.
#pragma once
#include <Arduino.h>

#define OTA_SIG_LEN   72
#define OTA_SIG_MAGIC "MUPSIG01"

void   otaSigBegin();
void   otaSigFeed(const uint8_t *data, size_t n);   // wszystkie bajty pliku po kolei
String otaSigFinish();                               // "" = podpis poprawny, inaczej opis bledu
