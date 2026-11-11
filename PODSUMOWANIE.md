# Monitor App – podsumowanie projektu

Stan na 11.11.2026 · firmware **v0.3.0** · sterownik **KinCony B8M (ESP32-S3)**

Monitor App to sterownik monitorujący poziom w zbiorniku paliwa (walec leżący) z przetwornika 4–20 mA. Pokazuje poziom w cm (opcjonalnie w litrach), uruchamia alarmy na wyjściach MOSFET, zapisuje logi na kartę SD, liczy zużycie i udostępnia dane przez stronę WWW, API, Modbus TCP i MQTT.

---

## 1. Zawartość paczki

| Ścieżka | Co to jest |
|---|---|
| `PODSUMOWANIE.md` | ten plik |
| `02_Monitor_App/` | **docelowy firmware** (Arduino, podział na moduły) + `README.md`, `PLAN.md`, `API.md`, `MODBUS.md` |
| `01_Test_v_0_01/` | program testowy wszystkich peryferiów płytki B8M (v0.01) |
| `dokumentacja/KinCony_B8M.md` | opis płytki: piny, adresy I2C, wejścia analogowe, wyjścia, otwarte pytania |
| `upload.ps1` | skrypt kompilacji i wgrywania przez arduino-cli (Windows/PowerShell) |

Pominięte: foldery `build/` (artefakty kompilacji, ok. 75 MB, i tak były starsze niż obecny kod), ustawienia lokalne `.claude/`. W paczce nie ma haseł użytkownika ani tokenów – w kodzie są tylko fabryczne wartości domyślne (patrz pkt 7).

## 2. Sprzęt

- **KinCony B8M**: ESP32-S3-WROOM-1U N16R8 (16 MB flash, 8 MB PSRAM), zasilanie 12–24 V DC.
- I2C: SDA GPIO8, SCL GPIO18.
  - PCF8575 @0x22: P0–P7 = 8 wyjść MOSFET, P8–P15 = 8 wejść DI (logika odwrócona).
  - ADS1115 @0x48: A1, A2 = 0–5 V; **A3, A4 = 4–20 mA**.
  - DS3231 @0x68 (RTC), EEPROM 24C02 @0x50, OLED SSD1306 128×64 @0x3C.
- Ethernet W5500, karta SD (SPI, wykrywanie GPIO5), RS485 (RX38/TX39), 1-Wire, wolne GPIO13/14/21, przycisk DOWNLOAD (GPIO0).
- **Przetwornik paliwa: 4–20 mA na zacisku A4.** Wartość rezystora pomiarowego wejść 4–20 mA nie jest podana przez producenta (domyślnie 250 Ω, do weryfikacji przy kalibracji).

## 3. Funkcje firmware (v0.3.0)

- **Zbiornik:** domyślnie jeden – „Zbiornik paliwa” na A4 (drugi kanał A3 jest w kodzie, ale domyślnie wyłączony). Próbkowanie co 100 ms, wynik co 1 s ze średniej z 2 s z odrzuceniem skrajnych, opcjonalne wygładzanie. Błąd czujnika przy < 3,6 mA lub > 21 mA.
- **Poziom w cm**, kalibracja dwupunktowa (bieżący odczyt lub ręcznie), przesunięcie sondy.
- **Litry (opcja):** walec leżący (średnica, długość), a także walec stojący, prostopadłościan, tabela cm→litry.
- **Progi alarmowe (cm):** KRYTYCZNY < MINIMALNY < ZA DUŻY, histereza, opóźnienie.
- **Wyjścia alarmowe:** OUT1 syrena, OUT2 lampka, OUT3 alarm zbiorczy (przypisanie dowolne), **programowy PWM** 100 Hz (jasność przed/po skasowaniu), tryb przerywany, sterowanie ręczne, opcjonalnie sprzętowy PWM lampki na GPIO13/14/21. Kasowanie: DI1, strona, Modbus, MQTT, API; przypomnienie po X min.
- **Start:** logo **MONITOR APP + www.monitorapp.pl** na OLED przez 5 s, w tym czasie test wyjść (pomijany po restarcie programowym).
- **Sieć:** Ethernet + WiFi (DHCP/statyczny), Access Point (zawsze / awaryjnie / wył.), mDNS `monitor.local`.
- **Czas:** DS3231 + NTP (2 serwery), strefa Polska z czasem letnim.
- **Logi na SD:** dzienne CSV, dziennik zdarzeń, rotacja, historia 24 h w PSRAM; wykresy i eksport CSV na stronie.
- **Statystyki zużycia:** zużycie dzienne, wykrywanie tankowań, średnie 7/30 dni, prognoza „wystarczy na X dni”, **alarm nagłego spadku** (Y cm w T min + okno nocne).
- **Modbus TCP** (port 502, zapis domyślnie wyłączony, mapa w `MODBUS.md`), **MQTT** z autodiscovery Home Assistant, **API HTTP** `/api/v1` z **tokenami** (Bearer, 3 poziomy uprawnień, skrót SHA-256).
- **System:** **OTA** przez stronę, **kopia ustawień** JSON (pobierz/wgraj), ustawienia fabryczne, watchdog 30 s, przyciemnianie OLED.
- **Ustawienia:** NVS + kopia kalibracji i progów w EEPROM 24C02 (CRC); nowe pola dopisywane na końcu struktury, więc aktualizacja nie kasuje konfiguracji.

## 4. Struktura modułów (`02_Monitor_App/`)

| Moduł | Odpowiada za |
|---|---|
| `02_Monitor_App.ino` | start, logo + test wyjść, pętla główna, watchdog, przycisk resetu |
| `hw.h/.cpp` | piny, muteks I2C, PCF8575, ADS1115, 24C02, DS3231, `FW_VERSION` |
| `config.h/.cpp` | struktura ustawień, NVS, EEPROM, hasło (sól + SHA-256), wartości domyślne |
| `cfgjson.h/.cpp` | ustawienia ↔ JSON (strona, kopia ustawień, import) |
| `sensors.h/.cpp` | pomiar, uśrednianie, litry, stany alarmowe |
| `outputs.h/.cpp` | logika alarmu, kasowanie, wyjścia, PWM |
| `stats.h/.cpp` | zużycie, tankowania, prognoza, nagły spadek |
| `logger.h/.cpp` | karta SD, logi CSV, zdarzenia, historia 24 h |
| `timesync.h/.cpp` | RTC, NTP |
| `net.h/.cpp` | Ethernet, WiFi, AP, mDNS |
| `display.h/.cpp` | OLED (U8g2) |
| `modbus.h/.cpp` | serwer Modbus TCP |
| `mqtt.h/.cpp` | MQTT + Home Assistant |
| `web.h/.cpp` | serwer WWW, logowanie, tokeny, API JSON |
| `web_pages.h`, `web_app.h` | strony WWW wbudowane w program, mapa Modbus CSV |
| `partitions.csv` | 2 × 3 MB OTA + ok. 9,9 MB LittleFS + coredump |

Razem ok. 4,4 tys. linii kodu C++ (bez stron WWW).

## 5. Kompilacja i wgrywanie

Wymagania: Arduino IDE 2 (arduino-cli w zestawie), rdzeń **esp32 3.3.x**, biblioteki **ArduinoJson 7.4.2**, **U8g2 2.35.30**, **PubSubClient**.

Płytka: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 16 MB, PSRAM OPI, partycje z `partitions.csv`.

Z folderu projektu (PowerShell):

```
.\upload.ps1 -Sketch 02_Monitor_App -Compile            # kompilacja + wgranie, port wykrywany automatycznie (USB VID 303A)
.\upload.ps1 -Sketch 02_Monitor_App -Compile -Port COM8 # z podanym portem
```

Albo bez kabla: strona urządzenia → Ustawienia → System → Aktualizacja, plik `02_Monitor_App\build\02_Monitor_App.ino.bin`.
Jeśli wgrywanie przez USB nie startuje: przytrzymaj DOWNLOAD i naciśnij RESET.

## 6. Dostęp do urządzenia

- Strona: `http://<IP>` (urządzenie w LAN: 192.168.0.12) lub `http://monitor.local`.
- Awaryjnie WiFi AP `MonitorApp-XXXX`, strona `http://192.168.4.1`.
- Log szeregowy: USB, 115200 bps.
- Zapomniane hasło: przytrzymaj DOWNLOAD 10 s → login, hasło i sieć wracają do fabrycznych (kalibracja zostaje).

## 7. Hasła i bezpieczeństwo

- Paczka **nie zawiera** haseł ustawionych przez użytkownika, tokenów API ani pliku kopii ustawień – są tylko w pamięci urządzenia.
- W kodzie są wyłącznie fabryczne wartości domyślne (opisane w README): logowanie admin/admin z wymuszoną zmianą przy pierwszym logowaniu, hasło AP `monitor123`, w programie testowym hasło AP `b8mtest123`.
- Kopia ustawień JSON pobierana ze strony **zawiera hasła WiFi/AP/MQTT** – nie udostępniaj jej osobom trzecim.
- HTTP nie jest szyfrowane – do dostępu zdalnego używaj VPN.

## 8. Stan prac i otwarte sprawy

- Zrealizowane wszystkie etapy planu (1–9 z `PLAN.md`); od v0.3.0 domyślnie jeden zbiornik (paliwo, A4).
- Do sprawdzenia w terenie:
  - kalibracja przetwornika i ustawienie rezystora pomiarowego (wartość bocznika B8M nieznana),
  - czy buzzer w lampce milknie przy obniżonej jasności PWM (zależy od lampki),
  - dopuszczalny prąd wyjść MOSFET (producent nie podaje jednoznacznie).
- Drobne niespójności w dokumentacji: `README.md` i `MODBUS.md` podają jeszcze wersję 0.2.0 i opisują dwa zbiorniki (ppoż. A3 + paliwo A4), a rejestr 0 Modbus w kodzie zwraca już 30 (= 0.3.0). Kod jest aktualny, dokumenty warto odświeżyć.
- Gdy broker MQTT jest niedostępny, próba połączenia co 15 s może na ok. 3 s spowolnić stronę (pomiar i alarmy działają niezależnie).
