# 02_Monitor_App – firmware Monitor App (KinCony B8M)

Plan: [PLAN.md](PLAN.md) · API: [API.md](API.md) · Modbus: [MODBUS.md](MODBUS.md). Wersja **0.2.0** – wszystkie etapy planu (1–9).

## Dostęp

| | |
|---|---|
| Strona | `http://<IP Ethernet lub WiFi>` (IP na OLED → ekran sieci) lub `http://monitor.local` |
| WiFi AP | SSID `MonitorApp-XXXX`, hasło domyślne `monitor123`, strona `http://192.168.4.1` |
| Logowanie | domyślnie `admin` / `admin` (wymuszona zmiana przy pierwszym logowaniu) |
| Zapomniane hasło | przytrzymaj **DOWNLOAD** 10 s (odliczanie na OLED) → login, hasło i sieć fabryczne, kalibracja zostaje |
| Log | USB (COM8), 115200 bps |

## Funkcje

- **Pomiar:** zbiornik ppoż. A3, paliwo A4 (wejście i typ 4–20 mA / 0–5 V wybieralne), próbki co 100 ms, wynik co 1 s ze średniej z 2 s z odrzuceniem skrajnych, opcjonalne wygładzanie. Błąd czujnika < 3,6 mA lub > 21 mA.
- **Kalibracja** dwupunktowa (bieżący odczyt jako MIN/MAX albo ręcznie), przesunięcie sondy, rezystor pomiarowy. **Litry** (opcja): walec leżący, walec stojący, prostopadłościan, tabela cm→litry.
- **Progi** w cm: KRYTYCZNY < MINIMALNY < ZA DUŻY, histereza, opóźnienie.
- **Wyjścia:** OUT1 syrena, OUT2 lampka, OUT3 alarm zbiorczy (domyślnie), funkcje dowolnie przypisywane, programowy PWM 100 Hz / 10 poziomów (jasność przed i po skasowaniu), tryb przerywany, sterowanie ręczne, opcjonalnie sprzętowy PWM lampki na GPIO13/14/21. **Kasowanie** DI1 (zbocze), strona, Modbus, MQTT, API; nowy gorszy stan ponownie uruchamia syrenę; przypomnienie po X min.
- **Start:** logo MONITOR APP + www.monitorapp.pl 5 s, test OUT1–OUT6 (pomijany po restarcie programowym).
- **Sieć:** Ethernet i WiFi (DHCP/statyczny), skanowanie WiFi, Access Point (zawsze / awaryjnie / wył.), mDNS. Zmiana sieci = restart.
- **Czas:** DS3231 (UTC), NTP (2 serwery, interwał), ustawianie z przeglądarki lub ręcznie, strefa Polska z czasem letnim.
- **Logi SD:** `/logs/RRRR/MM/RRRR-MM-DD.csv` co X min + przy zmianie stanu, dziennik zdarzeń `/events/RRRR-MM.csv`, rotacja (dni / < 10 % wolnego), historia 24 h w PSRAM. Strona Logi: wykres z powiększaniem, CSV, zużycie dzienne (słupki), zdarzenia, lista plików.
- **Statystyki:** zużycie dzienne (poziom o północy − teraz + tankowania), rozpoznawanie tankowań, średnie 7/30 dni, prognoza „wystarczy na X dni”, ostrzeżenie prognozy, **alarm nagłego spadku** (Y cm w T min + okno nocne), wstrzymanie na planowany pobór. `/stats/RRRR.csv`.
- **Modbus TCP** (port 502, mapa w MODBUS.md, zapis domyślnie wyłączony), **MQTT** + Home Assistant autodiscovery, **API** z tokenami (Bearer).
- **System:** OTA przez stronę, kopia ustawień JSON (pobierz/wgraj), restart, ustawienia fabryczne, watchdog 30 s, przyciemnianie OLED.
- **Ustawienia:** NVS + kopia kalibracji i progów w EEPROM 24C02 (CRC). Nowe pola dopisywane na końcu struktury, więc aktualizacja programu nie kasuje ustawień.

## Kompilacja i wgrywanie

```
.\upload.ps1 -Sketch 02_Monitor_App -Compile
```

albo przez stronę: Ustawienia → System → Aktualizacja (plik `build\02_Monitor_App.ino.bin`).
Partycje: `partitions.csv` (2 × 3 MB OTA + 9,9 MB LittleFS). Biblioteki: ArduinoJson 7.4.2, U8g2 2.35.30, PubSubClient.

## Pliki

| Plik | Zawartość |
|---|---|
| `02_Monitor_App.ino` | start, logo + test wyjść, pętla, watchdog, przycisk resetu |
| `hw.h` | piny, muteks I2C, PCF8575, ADS1115, 24C02, DS3231 |
| `config.h`, `cfgjson.h` | ustawienia, NVS, EEPROM, hasło, JSON (strona, kopia) |
| `sensors.h` | pomiar, uśrednianie, litry, stany |
| `logger.h` | SD, logi CSV, zdarzenia, historia 24 h |
| `timesync.h` | RTC, NTP |
| `stats.h` | zużycie, tankowania, prognoza, nagły spadek |
| `outputs.h` | alarm, kasowanie, wyjścia, PWM |
| `net.h` | Ethernet, WiFi, AP |
| `display.h` | OLED |
| `modbus.h`, `mqtt.h` | Modbus TCP, MQTT/HA |
| `web.h`, `web_pages.h`, `web_app.h` | serwer, API, strony, mapa Modbus CSV |

## Uwagi

- Bez karty SD działają pomiar, alarmy i wykres 24 h; logi, statystyki dzienne i dziennik na karcie wymagają karty (FAT32).
- Rezystor pomiarowy wejść 4–20 mA B8M nie jest podany przez producenta – ustaw go w Przetwornikach tak, by przy znanym prądzie odczyt mA był poprawny (wpływa na wykrywanie błędu czujnika, nie na kalibrację cm).
- PWM na wyjściach idzie przez ekspander I2C; czy buzzer w lampce milknie przy obniżonej jasności, trzeba sprawdzić na konkretnej lampce.
- Gdy broker MQTT jest niedostępny, próba połączenia co 15 s może na ok. 3 s spowolnić stronę (pomiar i wyjścia działają niezależnie).
