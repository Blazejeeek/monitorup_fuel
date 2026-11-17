# MonitorUp.pl – funkcje i historia wersji

Jeden plik dla wszystkich wersji: na górze stan aktualny, niżej zmiany w każdej wersji (najnowsza pierwsza).
Repozytorium: https://github.com/Blazejeeek/monitorup_fuel (gałąź `dev`). Karta projektu: #139 na tablicy „Agent”.

## Wersja aktualna

| | |
|---|---|
| Oprogramowanie (SW) | **0.5.4** (rejestr Modbus 0 = 54) |
| Płytka (HW) | **1.0** – KinCony B8M (ESP32-S3-WROOM-1U N16R8, 16 MB flash, 8 MB PSRAM) |
| Plik do aktualizacji przez stronę | `MonitorUp_v<SW>.bin` (Ustawienia → System → Aktualizacja) |
| Kompilacja | Windows: `upload.ps1 -Sketch 02_Monitor_App -Compile` · Linux: `./build.sh 02_Monitor_App` (rdzeń esp32 3.3.x) |

## Funkcje (stan na 0.5.4)

**Pomiar**
- Jeden zbiornik: przetwornik 4–20 mA na A4, rezystor pomiarowy 150 Ω na stałe (bez wyboru wejścia, typu i rezystora).
- Gęstość cieczy (woda 1000, olej napędowy ok. 840 kg/m³): punkty MIN/MAX zakresu czujnika zapisywane jako słup wody.
- Tabela kalibracji prądu A4 (do 24 punktów, plik JSON z pomiaru wzorcowym miernikiem, `/api/v1/calib/ma`) – zapisywana osobno (NVS + kopia w EEPROM), aktualizacja jej nie kasuje.
- Próbki co 100 ms, wynik co 1 s ze średniej z 2 s z odrzuceniem skrajnych, opcjonalne wygładzanie. Błąd czujnika poniżej 3,6 mA lub powyżej 21 mA.
- Kalibracja: wpisz aktualny poziom zmierzony miarką [cm] i kliknij „Kalibruj” – korekta przesunięcia sondy (zakres czujnika i gęstość bez zmian). Kalibracja dwupunktowa MIN/MAX jako opcja zaawansowana.
- Litry (opcja): walec leżący, walec stojący, prostopadłościan, tabela cm → litry.

**Alarmy i wyjścia**
- Tryb alarmu do wyboru: **zbiornik ppoż. wody** (KRYTYCZNY ≤ 90 %, MINIMALNY ≤ 95 %, ZA DUŻY ≥ 98 %), **zbiornik ppoż. paliwa** (80 / 90 / 95 %) albo **własne** progi w % wysokości zbiornika; histereza [cm], opóźnienie.
- Alarm nagłego spadku (wyciek / kradzież): spadek o Y cm w T min, okno nocne z czulszym progiem.
- Wyjścia na stałe: WY1 POPRAWNY (kontrolka zielona), WY2 MINIMALNY (pomarańczowa), WY3 KRYTYCZNY (czerwona), WY4 buzer (alarm WY3 lub błąd czujnika, gra w rytmie czerwonej kontrolki, wyciszenie wejściem IN1, stroną, Modbus, MQTT), WY5 przekaźnik usterki zbiorczej (zasilony = brak usterki).
- Usterka zbiorcza – wybór przyczyn w Ustawieniach → Wyjścia, z podglądem na żywo: błąd czujnika, ADS1115, karta SD (domyślnie), a także czas nieustawiony, brak sieci, brak połączenia MQTT, PCF8575.
- Czerwona kontrolka (WY3): świeci ciągle – poziom krytyczny; miga 1× na sekundę – ZA DUŻY (przepełnienie); 2× na sekundę – NAGŁY SPADEK; 3× na sekundę – usterka czujnika (zielona i pomarańczowa zgaszone, buzer).
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
- OTA przez stronę i API **tylko dla plików podpisanych kluczem MonitorUp.pl** (ECDSA P-256; plik bez podpisu albo zmieniony jest odrzucany, dotychczasowa wersja zostaje), ustawienia fabryczne (z zachowaniem kalibracji), reset hasła przyciskiem DOWNLOAD (10 s), watchdog 30 s.
- Ustawienia w NVS + kopia kalibracji i progów w EEPROM 24C02; nowe pola na końcu struktury – aktualizacja nie kasuje konfiguracji.

## Historia wersji

### 0.5.5 – w przygotowaniu (niewydana, nie wgrana)
- Logi: przycisk **„Pobierz pomiary (CSV)”** – wszystkie pomiary z wybranego dnia, miesiąca albo roku w jednym pliku (`/api/v1/logs/csv?from=…&to=…`); „Pobierz podsumowanie (CSV)” – wartości dobowe/miesięczne jak dotąd.
- W pliku pomiarów dni bez danych między dniami z danymi oznaczone wierszem `RRRR-MM-DD;brak danych` (dni przed pierwszym i po ostatnim pliku pomijane).
- W trakcie wysyłania logów (wiele dni) obsługiwane są alarmy (co 100 ms) i przycisk kasowania (co 50 ms) – wcześniej pętla główna stała przez cały czas pobierania.
- **Sygnalizacja wg nakładki panelu:** WY1 NORMA (zielona) – stan normalny; WY2 OSTRZEŻENIE (pomarańczowa) – ciągłe: poziom minimalny, przerywane 1×/s: awaria czujnika; WY3 ALARM (czerwona) – ciągłe: poziom krytyczny, przerywane 1×/s: przepełnienie, przerywane szybkie 4×/s: nagły spadek. Wcześniej awaria czujnika = czerwona 3×/s, nagły spadek = 2×/s.
- Strona i wyświetlacz: nazwy stanów jak na nakładce (NORMA, POZIOM MINIMALNY, POZIOM KRYTYCZNY, PRZEPEŁNIENIE, AWARIA CZUJNIKA, NAGŁY SPADEK POZIOMU); awaria czujnika i przepełnienie w kolorach kontrolek. Nazwy w Modbus/MQTT/API bez zmian (zgodność z BMS).
- Wgrane 17.11.2026 19:01 przez OTA z Pi masim. Test zadajnikiem: normalny (WY1), minimalny (WY2 ciągle, bez buzera), krytyczny (WY3 + buzer ciągle), przepełnienie (WY3 + buzer 1,00×/s), kasowanie (buzer cichnie, WY3 dalej miga) – OK.
- Nagły spadek (`droptest.sh`): WY3 i buzer 4,00×/s, kasowanie wycisza buzer – OK. Pobieranie CSV (`stanowisko/csvtest.py`): dzień 11,7 kB / 0,1 s, miesiąc 0,4 s, rok (321 dni, 2 z danymi) 2,6 s, jeden nagłówek, BOM – OK.
- Do zrobienia przed wydaniem: awaria czujnika (ręczne odłączenie A4 – pomarańczowa i buzer 1×/s), test e-mail (dane SMTP od użytkownika); `sym.py` na Pi pokazuje jeszcze stary opis „czerwona miga 2×/s” (tylko tekst skryptu).
- **Powiadomienia e-mail** (Ustawienia → Powiadomienia): serwer SMTP (SSL/TLS 465, STARTTLS 587 albo bez szyfrowania, sprawdzanie certyfikatu wbudowanym pakietem CA), login, hasło (jak inne hasła – w kopii tylko szyfrowanej), nadawca; do 5 odbiorców, **każdy z własnym wyborem**: poziom krytyczny, przepełnienie, nagły spadek, awaria czujnika, poziom minimalny, powrót do normy, skasowanie alarmu, usterka zbiorcza, raport dzienny (o wybranej godzinie). Ten sam rodzaj zdarzenia najwyżej raz na X min (domyślnie 15). Wysyłka w osobnym zadaniu (`mail.cpp`, własny klient SMTP bez bibliotek), ponawianie 1/5/15/60 min do ok. 24 h, wynik na stronie i w dzienniku zdarzeń, przycisk „Wyślij test” (`POST /api/v1/mail/test {"to":…}`, tylko po zalogowaniu). Ustawienia maili zmienia tylko zalogowany użytkownik (tokeny API – nie).
- Wersja w kodzie: `0.5.5-dev` (rejestr Modbus 0 = 55) – plik `MonitorUp_v0.5.5-dev.bin`, niewydany.
- Buzer (decyzja użytkownika 17.11.2026): stany ALARM (krytyczny, przepełnienie, nagły spadek) **oraz awaria czujnika**; gra w rytmie migającej kontrolki; przy poziomie minimalnym cicho.

### 0.5.4 – 16.11.2026
- Aktualizacja przez stronę / API przyjmuje tylko pliki podpisane kluczem MonitorUp.pl: podpis ECDSA P-256 nad SHA-256 obrazu doklejony na końcu pliku (`MUPSIG01`), sprawdzany przed przełączeniem partycji startowej. Odrzucenie zapisywane w dzienniku (OTA).
- `build.sh` tworzy podpisany `MonitorUp_v<wersja>.bin` (`sign_fw.py`, klucz prywatny tylko na serwerze budowania); sprawdzenie: `sign_fw.py --sprawdz plik.bin`.
- Wgranie przez USB i odczyt programu przez USB nie są blokowane (zabezpieczenia eFuse świadomie nie włączone).
- Test przez API: plik bez podpisu i plik zmieniony (1 bit) odrzucone bez restartu, plik podpisany wgrany.

### 0.5.3 – 16.11.2026
- Karta SD: po usunięciu logów ze strony (albo zniknięciu pliku) zapis pomiarów zakłada plik i katalogi od nowa – wcześniej co 5 min „Błąd zapisu karty SD” i brak pomiarów do północy.
- Kalibracja, gęstość, zakres czujnika, tabela kalibracji prądu: po zmianie przeliczania poziomu tankowanie i nagły spadek liczone od nowa (zdarzenie STATYSTYKI) – bez fałszywego „TANKOWANIA” i alarmu nagłego spadku z buzerem.
- Wgrane przez API; test: kalibracja +10 cm i −10 cm bez fałszywych zdarzeń, zapis SD o 14:00 i 14:05 bez błędów.

### 0.5.2 – 16.11.2026
- Buzer (WY4) gra w rytmie czerwonej kontrolki: ciągle (krytyczny), 1×/s (przepełnienie), 2×/s (nagły spadek), 3×/s (usterka czujnika); równe piknięcia, bez serii podobnych do sygnału ewakuacji.
- Wejście IN1: po starcie przyjmowany jest jego bieżący stan – stale aktywne IN1 nie wycisza już buzera przy każdym uruchomieniu.
- Strona i nakładka A4: opis „buzer gra w rytmie czerwonej kontrolki”.
- Wgrane przez API; zmierzone przez API: kontrolka i buzer razem – ciągle / 1,00 / 2,00 / 3,05 × na sekundę; usterka czujnika (odłączenie A4): zielona i pomarańczowa zgaszone, WY5 puszcza.

### 0.5.1 – 16.11.2026
- Wykrywanie tankowania: po zakończeniu tankowania okno 15 min liczy się od jego końca (wcześniej poziom sprzed zakończenia mógł od razu rozpocząć kolejne „tankowanie” i na kolejne 15 min wstrzymać alarm nagłego spadku).
- Instrukcja testów `TESTY.md` i skrypty stanowiska `stanowisko/` (zadajnik + weryfikacja przez API, pomiar rytmu migania).
- Wgrane przez API; test zadajnikiem: poprawny, minimalny, krytyczny (czerwona ciągle), przepełnienie (1,00×/s), nagły spadek (1,99×/s), wyciszenie – OK. Usterki czujnika zadajnik pasywny nie wywoła (4,05–20,05 mA) – do sprawdzenia ręcznie.

### 0.5.0 – 16.11.2026
- Czerwona kontrolka sygnalizuje rodzaj alarmu: ciągle = krytyczny, 1×/s = przepełnienie, 2×/s = nagły spadek, 3×/s = usterka czujnika (wcześniej przy usterce migały WY1–WY3).
- Kalibracja jednym punktem: aktualny poziom [cm] + „Kalibruj” (korekta przesunięcia sondy); dwa punkty w „zaawansowane”. API: `point:"level"`.
- Alarmy: tryb zbiornik ppoż. wody / zbiornik ppoż. paliwa / własne; progi w % wysokości zbiornika (podgląd w cm). Nowe pola na końcu ustawień (`alarmMode`, `thrPct`); progi % starszych urządzeń przeliczane z cm. Zapis progów w cm (API, Modbus) = tryb własny.
- Wyjścia opisane: WY1 POPRAWNY, WY2 MINIMALNY, WY3 KRYTYCZNY + opis migania czerwonej kontrolki (strona: Wyjścia i Alarmy).
- Nakładka A4: nazwy POPRAWNY / MINIMALNY / KRYTYCZNY i legenda migania czerwonej kontrolki.
- Wgrane przez API (Pi masim); sprawdzone przez API: tryby i progi %, odrzucenie złych progów, stare API w cm, kalibracja +5 cm (175,5 → 180,5 cm) i przywrócenie.

### 0.4.9 – 16.11.2026
- Błąd czujnika: WY1–WY3 migają 3 razy na sekundę (było 1 Hz); miganie w zadaniu PWM, więc rytm jest równy.
- Nagłówek strony: „MonitorUp.pl” jednym napisem (wcześniej odstęp przed „.pl”).
- Wgrane na płytkę testową przez API (OTA z Pi masim): start OK, 18,05 mA, stan NORMALNY.

### 0.4.8 – 13.11.2026
- Tabela kalibracji prądu A4 (surowy odczyt → mA, do 24 punktów), import/eksport JSON, kopia w EEPROM.
- Jeden zbiornik 4–20 mA na A4, rezystor 150 Ω na stałe; gęstość cieczy, punkty MIN/MAX jako słup wody.
- Wyjścia na stałe (WY1 OK, WY2 MIN, WY3 KRYT/ZA DUŻY/SPADEK, WY4 buzer, WY5 przekaźnik usterki); IN1 wycisza buzer, także przy błędzie czujnika.
- Czytelny komunikat, gdy wysokość zbiornika jest mniejsza niż próg ZA DUŻY; wysokość także w zakładce Alarmy.
- Uwaga: rejestr Modbus 0 w tej wersji omyłkowo = 47 (poprawione w 0.4.9 = 49).

### 0.4.7 – 11.11.2026
- Usunięte komunikaty watchdoga „task not found” przy starcie (reset watchdoga tylko w zadaniach, które są pod nim zapisane).
- Log szeregowy: informacja o dopisaniu wiersza podsumowania doby, miesiąca i roku oraz „Podsumowania: aktualne”.
- Wgrane na płytkę testową (COM8): start bez błędów, podsumowania aktualne (brak plików z wcześniejszych dni).

### 0.4.6 – 11.11.2026
- Przy błędzie czujnika migają kontrolki WY1–WY3 (1 Hz); przełącznik „Przy błędzie czujnika migają kontrolki WY1–WY3” w Ustawieniach → Wyjścia (domyślnie włączony). API: `alarm.faultBlink`.

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
- Do potwierdzenia: progi stałe zbiorników ppoż. (woda 90/95/98 %, paliwo 80/90/95 %). Do obejrzenia na żywo: rytmy migania czerwonej kontrolki.
- W terenie: kalibracja przetwornika, działanie buzzera lampki przy obniżonej jasności, dopuszczalny prąd wyjść MOSFET.
