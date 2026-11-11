// MonitorUp - podsumowania hierarchiczne do wykresow wieloletnich (0.4.5)
//   /logs/RRRR/MM/RRRR-MM-DD.csv  pomiary co X min (jak dotad) – widok „Dzien”
//   /stats/miesiac_RRRR-MM.csv    1 wiersz na dobe   – dopisywany po zakonczeniu doby      – widok „Miesiac”
//   /stats/rok_RRRR.csv           1 wiersz na miesiac – dopisywany po zakonczeniu miesiaca – widok „Rok”
//   /stats/lata.csv               1 wiersz na rok    – dopisywany po zakonczeniu roku      – widok „Wszystkie lata”
// Brakujace wiersze (urzadzenie wylaczone o polnocy, pierwsze uruchomienie) uzupelniane sa w tle z plikow nizszego poziomu.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void summaryKick();                // zlec sprawdzenie brakow (start, zmiana doby)
void summaryLoop();                // co 1 s z petli glownej: najwyzej jeden plik na wywolanie
// view: "years" | "year" (y) | "month" (y, m). Wynik: rows[], biezacy okres oznaczony jako niepelny.
String summaryView(const String &view, int y, int m, JsonObject out);
