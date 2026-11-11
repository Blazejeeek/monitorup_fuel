# MonitorUp.pl – funkcje i historia wersji

Jeden plik dla wszystkich wersji: na górze stan aktualny, niżej zmiany w każdej wersji (najnowsza pierwsza).
Repozytorium: https://github.com/Blazejeeek/monitorup_fuel (gałąź `dev`). Karta projektu: #139 na tablicy „Agent”.

## Wersja aktualna

| | |
|---|---|
| Oprogramowanie (SW) | **0.4.5** (rejestr Modbus 0 = 45) |
| Płytka (HW) | **1.0** – KinCony B8M (ESP32-S3-WROOM-1U N16R8, 16 MB flash, 8 MB PSRAM) |
| Plik do aktualizacji przez stronę | `MonitorUp_v<SW>.bin` (Ustawienia → System → Aktualizacja) |
| Kompilacja | Windows: `upload.ps1 -Sketch 02_Monitor_App -Compile` · Linux: `./build.sh 02_Monitor_App` (rdzeń esp32 3.3.x) |

## Funkcje (stan na 0.4.5)

**Pomiar**
- Przetwornik 4–20 mA na A4 (zbiornik paliwa, walec leżący); drugi kanał A3 do włączenia w ustawieniach. Wejście i typ (4–20 mA / 0–5 V) wybieralne.
- Próbki co 100 ms, wynik co 1 s ze średniej z 2 s z odrzuceniem skrajnych, opcjonalne wygładzanie. Błąd czujnika poniżej 3,6 mA lub powyżej 21 mA.
- Kalibracja dwupunktowa, przesunięcie sondy, rezystor pomiarowy (domyślnie 150 Ω, ustawiany w Przetwornikach).
- Litry (opcja): walec leżący, walec stojący, prostopadłościan, tabela cm → litry.

**Alarmy i wyjścia**
- Progi w cm: KRYTYCZNY < MINIMALNY < ZA DUŻY (maksymalny), histereza, opóźnienie.
- Alarm nagłego spadku (wyciek / kradzież): spadek o Y cm w T min, okno nocne z czulszym progiem.
- Domyślne wyjścia: OUT1 poziom minimalny, OUT2 poziom maksymalny, OUT3 poziom krytyczny, OUT4 usterka zbiorcza (przekaźnik), OUT5 buzzer (milknie po skasowaniu). Funkcje wyjść do zmiany w Ustawieniach → Wyjścia.
- Usterka zbiorcza – wybór przyczyn w Ustawieniach → Wyjścia, z podglądem na żywo: błąd czujnika, ADS1115, karta SD (domyślnie), a także czas nieustawiony, brak sieci, brak połączenia MQTT, PCF8575.
- PWM programowy 100 Hz (jasność przed i po skasowaniu), tryb przerywany, sterowanie ręczne, opcjonalny sprzętowy PWM lampki (GPIO13/14/21).
- Kasowanie alarmu do wyboru: ręczne (DI1, strona, Modbus, MQTT, API; przypomnienie po X min) albo automatyczne – bez kasowania, alarm gaśnie po powrocie poziomu do normy.

**Wyświetlacz OLED**
- Start: ikona zbiornika, „MonitorUp.pl” w jednej linii, pod spodem SW i HW, adres www.monitorup.pl, test wyjść (pomijany po restarcie programowym).
- Ekrany: zbiornik (poziom, %, litry, stan, słupek z progami) i sieć (ETH, WiFi, AP, wersje SW/HW); górny pasek „MonitorUp.pl” i ikony sieci.

**Strona WWW i integracje (sieć lokalna, dla BMS)**
- Pulpit: poziom, zużycie dziś, średnio 7 dni, ostatnie tankowanie, wykres 24 h. Logi, ustawienia, informacje jedna pod drugą. Napis „MonitorUp.pl” (duże M i U) na ekranie logowania i w nagłówku.
- Logowanie z blokadą po 5 błędnych próbach, wymuszona zmiana admin/admin; tokeny API (Bearer) z uprawnieniami, terminem i limitem zapytań.
- Modbus TCP (port 502, domyślnie wyłączony, zapis domyślnie wyłączony), MQTT (osobne zadanie, ponawianie 15 s → 5 min, bez autodiscovery Home Assistant), API `/api/v1` z opisem zapytań i przykładami w Ustawieniach → API.
- Nagłówki bezpieczeństwa strony. HTTP, Modbus i MQTT bez szyfrowania – tylko sieć lokalna.

**Dane i system**
- Wykresy w zakładce Logi – cztery widoki, zawsze z jednego pliku: **Dzień** (pomiary co 5 min z pliku dnia), **Miesiąc** (wartości dobowe), **Rok** (12 miesięcy), **Wszystkie lata** (do 10 lat). Średni poziom z paskiem min–max, przy włączonej pojemności: objętość (oś lewa) i wypełnienie 0–100 % (oś prawa, inny kolor). Pod spodem zużycie dla tego samego okresu; pobieranie CSV.
- Pliki: `/logs/RRRR/MM/RRRR-MM-DD.csv` (pomiary), `/stats/miesiac_RRRR-MM.csv` (1 wiersz na dobę, po zakończeniu doby), `/stats/rok_RRRR.csv` (1 wiersz na miesiąc, po zakończeniu miesiąca), `/stats/lata.csv` (1 wiersz na rok, po zakończeniu roku). Brakujące wiersze (urządzenie wyłączone o północy, pierwsze uruchomienie) urządzenie uzupełnia samo w tle. Okres bieżący liczony na żywo.
- Przechowywanie pomiarów domyślnie 10 lat (3650 dni); ok. 120–150 MB karty przy zapisie co 5 min.
- Logi na karcie SD (dzienne CSV, dziennik zdarzeń, rotacja), historia 24 h w PSRAM, zużycie dzienne i tankowania (zawsze włączone; bez prognozy).
- Karta SD: sprawdzana co 10 s i przy każdym zapisie, bez karty próba montowania co 30 s; wyjęcie lub uszkodzona partycja nie zatrzymują pomiaru i alarmów (opis w Ustawieniach → Logi, „Test karty SD”).
- Kopia ustawień: bez haseł albo z hasłami zaszyfrowana hasłem kopii (AES-256-GCM, PBKDF2-SHA256); starsze kopie da się wgrać.
- OTA przez stronę, ustawienia fabryczne (z zachowaniem kalibracji), reset hasła przyciskiem DOWNLOAD (10 s), watchdog 30 s.
- Ustawienia w NVS + kopia kalibracji i progów w EEPROM 24C02; nowe pola na końcu struktury – aktualizacja nie kasuje konfiguracji.

## Historia wersji

### 0.4.5 – 11.11.2026
- Podsumowania hierarchiczne: doba → `/stats/miesiac_RRRR-MM.csv`, miesiąc → `/stats/rok_RRRR.csv`, rok → `/stats/lata.csv`; uzupełnianie braków w tle.
- Zakładka Logi: wybór widoku Wszystkie lata / Rok / Miesiąc / Dzień zamiast dowolnego zakresu dat; zużycie i CSV dla wybranego okresu.
- Wykres z pojemnością: objętość + wypełnienie 0–100 % na drugiej osi (inny kolor).
- Przechowywanie pomiarów domyślnie 10 lat. API: `GET /api/v1/summary?view=years|year|month&y=&m=`.

### 0.4.4 – 11.11.2026
- „MonitorUp.pl” z dużymi literami M i U na ekranie logowania i w nagłówku strony.
- „Skasowany” zamiast „skwitowany” (strona, OLED, dziennik); w MQTT nowy klucz `skasowany`, stary `skwitowany` zostaje dla zgodności.
- Nowe ustawienie: kasowanie alarmu ręczne albo automatyczne (bez kasowania, alarm gaśnie po powrocie do normy). API: `alarm.ackAuto`, w statusie `alarm.auto`.
- Usunięte: wstrzymanie alarmu spadku na 2 h („planowany pobór”, także `POST /api/v1/alarm/suspend`), prognoza „wystarczy na” i jej ostrzeżenie (Modbus zwraca „brak danych”), przełącznik statystyk (zużycie zawsze liczone), autodiscovery Home Assistant.
- Ustawienia → API: tabela zapytań z uprawnieniami i przykłady `curl`.
- Ustawienia → Logi: opis, jak urządzenie pilnuje karty SD (co ile, wyjęcie w trakcie, uszkodzona partycja, pełna karta).

### 0.4.3 – 11.11.2026
- OLED, start: „MonitorUp.pl” w jednej linii, pod spodem wersja oprogramowania (SW) i płytki (HW 1.0).
- Wersje SW i HW także na ekranie sieci, w Ustawieniach → System, w `/api/v1/status` (`fw`, `hw`) i w Home Assistant.
- Ten plik zastępuje opisy zmian w README.

### 0.4.2 – 11.11.2026
- Ustawienia → Wyjścia → „Usterka zbiorcza – co sygnalizuje”: wybór przyczyn z podglądem stanu na żywo.
- API: `alarm.faultMask` w ustawieniach, `fault` w statusie.

### 0.4.1 – 11.11.2026
- Nazwa MonitorUp.pl wszędzie (strona, OLED, nazwa urządzenia, SSID AP `MonitorUp.pl-XXXX`); dawne nazwy domyślne zamieniane przy starcie, prefiks MQTT bez zmian.
- Domyślne wyjścia OUT1–OUT5 (minimalny, maksymalny, krytyczny, usterka zbiorcza, buzzer); nowe funkcje wyjść.
- Modbus TCP domyślnie wyłączony. Pulpit bez „wystarczy na” (prognoza nadal w MQTT i Modbus).
- Informacje o sieci, karcie SD i systemie jedna pod drugą. OLED: mniejszy napis „BŁĄD CZUJNIKA”.

### 0.4.0 – 11.11.2026
- Nazwa MonitorUp (www.monitorup.pl). Rezystor pomiarowy domyślnie 150 Ω.
- MQTT we własnym zadaniu – strona i Modbus nie zwalniają przy niedostępnym brokerze.
- Kopia ustawień z hasłami tylko zaszyfrowana. Nagłówki bezpieczeństwa. Skrypt `build.sh` (kompilacja na Linuksie).

### 0.3.0 – 11.11.2026 (wersja wyjściowa z paczki)
- Wszystkie etapy 1–9 planu: pomiar, kalibracja, litry, progi, wyjścia z PWM, OLED, sieć, czas, logi SD, statystyki, Modbus TCP, MQTT/HA, API z tokenami, OTA, kopia ustawień, watchdog. Domyślnie jeden zbiornik (paliwo, A4).

## Znane sprawy otwarte
- Do ustalenia: czy przy błędzie czujnika mają mrugać wszystkie wyjścia.
- Przy starcie 2× komunikat watchdoga „task not found” (nieszkodliwy, do poprawy).
- W terenie: kalibracja przetwornika, działanie buzzera lampki przy obniżonej jasności, dopuszczalny prąd wyjść MOSFET.
