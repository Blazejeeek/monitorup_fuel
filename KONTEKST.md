# MonitorUp.pl – kontekst projektu do kolejnych iteracji

Ten plik ma pozwolić podjąć pracę bez czytania całej historii rozmów. Aktualizowany przy każdej wersji, razem z `FUNKCJE.md` (funkcje i historia wersji).

## Co to jest
Firmware sterownika poziomu paliwa **MonitorUp.pl** (www.monitorup.pl) na płytce **KinCony B8M** (ESP32-S3-WROOM-1U N16R8, 16 MB flash, 8 MB PSRAM). Oznaczenie sprzętu: **HW 1.0** (`HW_NAME`, `HW_VERSION` w `hw.h`). Przetwornik 4–20 mA na A4, zbiornik paliwa (walec leżący). Urządzenie pracuje w sieci lokalnej, dostęp dla BMS przez Modbus TCP / MQTT / API.

## Gdzie co jest
| Co | Gdzie |
|---|---|
| Repozytorium | https://github.com/Blazejeeek/monitorup_fuel – praca tylko na `dev`, `main` = pusty start (produkcja wyłącznie na polecenie) |
| Lokalnie (serwer agenta) | `~/projects/monitor-app` |
| Karta projektu | #139 na tablicy „Agent” (Nextcloud Deck), notatki agenta: `~/projects/_karty/139/NOTATKI.md` |
| Firmware | `02_Monitor_App/` (moduły `.h/.cpp`), program testowy peryferiów: `01_Test_v_0_01/` |
| Dokumentacja | `FUNKCJE.md` (funkcje + historia), `02_Monitor_App/README.md`, `API.md`, `MODBUS.md`, `PLAN.md` (plan wyjściowy), `dokumentacja/KinCony_B8M.md` |

## Moduły firmware
`02_Monitor_App.ino` start i pętla · `hw` piny, I2C, wersje, `wdtFeed()` · `config` struktura ustawień (NVS + EEPROM) · `cfgjson` ustawienia ↔ JSON · `sensors` pomiar · `outputs` alarmy, wyjścia, usterka zbiorcza, miganie WY1–WY3 · `stats` zużycie, tankowania, alarm nagłego spadku · `summary` podsumowania doba/miesiąc/rok/lata · `logger` karta SD · `display` OLED · `net` sieć · `modbus` · `mqtt` (osobne zadanie FreeRTOS + kolejka do pętli głównej) · `web`, `web_pages.h`, `web_app.h` strona i API · `crypt` szyfrowanie kopii ustawień.

## Zasady, których trzeba pilnować
- **Ustawienia:** nowe pola tylko na **końcu** `struct Config` (config.h) i domyślna wartość w `config.cpp`; starsze bloki NVS wczytują się jako prefiks. Nie zmieniaj kolejności ani typów istniejących pól.
- **Funkcje wyjść (`OutFunc`)** i bity usterki (`FaultBit`) tylko dopisywane na końcu (wartości są zapisane w urządzeniach).
- **Domyślne wartości** dotyczą nowych urządzeń i ustawień fabrycznych – działające płytki zachowują zapisane ustawienia.
- **Zgodność integracji:** nie zmieniaj adresów rejestrów Modbus ani kluczy MQTT; usunięte dane zwracają „brak danych” (np. prognoza = 65535), stary klucz MQTT `skwitowany` zostaje obok `skasowany`.
- **Watchdog:** w kodzie używaj `wdtFeed()` zamiast `esp_task_wdt_reset()`.
- **Karta SD:** wolno pisać tylko z pętli głównej (zadanie MQTT przekazuje zdarzenia kolejką).
- **Strona:** po zmianach w `web_app.h` sprawdź składnię JS (`node --check` na bloku `R"JS(…)JS"`).
- Nazwa produktu zawsze **„MonitorUp.pl”** (duże M i U).

## Kompilacja i wgrywanie
- Kompilacja (serwer agenta): `./build.sh 02_Monitor_App` – arduino-cli `~/.local/bin`, rdzeń esp32 **3.3.12**, biblioteki ArduinoJson 7.4.2, U8g2 2.35.30, PubSubClient 2.8 (program testowy: OneWire, DallasTemperature). Windows: `upload.ps1`.
- Partycje: `partitions.csv` (2 × 3 MB OTA, LittleFS, coredump).
- Wgranie przez stanowisko zdalne (Windows DESKTOP-IQTQJI5 przez WireGuard, `ssh esp-lab`, płytka na COM8): sam program pod `0x10000` – ustawienia (NVS) zostają:
  `scp 02_Monitor_App/build/02_Monitor_App.ino.bin esp-lab:C:/esp/fw/monitorup/` i `esptool --chip esp32s3 --port COM8 -b 921600 write-flash -z 0x10000 02_Monitor_App.ino.bin`. Pełny zestaw (pierwsze wgranie): `0x0` bootloader, `0x8000` partycje, `0xe000` boot_app0, `0x10000` program. Nie wgrywaj `merged.bin` (16 MB) – kasuje ustawienia.
- Log: `~/bin/esp-remote monitor --id COM8 -s 60` (po resecie pierwsze linie giną – natywne USB).
- Bez kabla: strona urządzenia → Ustawienia → System → Aktualizacja, plik `MonitorUp_v<SW>.bin`.
- Strona sterownika (192.168.0.12) nie jest osiągalna z sieci stanowiska, dopóki stanowisko nie dostanie adresu 192.168.0.x (polecenie w karcie #139).

## Wydanie wersji (po każdej zmianie)
1. `FW_VERSION` w `hw.h`, `FW_NUM` w `modbus.cpp`, wpis w `FUNKCJE.md`, aktualizacja tego pliku, `README.md`/`MODBUS.md`/`API.md`, gdy dotyczy.
2. Kompilacja bez ostrzeżeń, `node --check` dla JS, commit na `dev`, **tag `vX.Y.Z`**, `git push origin dev --tags`.
3. Karta #139: załącznik `MonitorUp_vX.Y.Z.bin` (stare zostają), podmiana `MonitorUp_funkcje_i_wersje.md` (najpierw usuń stary załącznik `?type=file` i plik `Deck/…` przez WebDAV, inaczej Nextcloud dopisze „(2)”), komentarz z opisem wersji, aktualny opis karty (wersja, czy wgrana, lista ✋).

## Powrót do wcześniejszej wersji (git)
- Lista wersji: `git tag -l` (każda wersja ma tag `vX.Y.Z`).
- Podgląd całej wersji: `git switch --detach v0.4.5` (powrót: `git switch dev`).
- Jeden plik z wersji: `git checkout v0.4.5 -- 02_Monitor_App/outputs.cpp`.
- Różnice między wersjami: `git diff v0.4.5 v0.4.7 -- 02_Monitor_App/`.
- Wycofanie zmiany na `dev` bez przepisywania historii: `git revert <commit>`.
- Gotowe pliki `.bin` każdej wersji są też w załącznikach karty #139.

## Stan na 11.11.2026
- SW **0.4.7** wgrane na płytkę testową (COM8, MAC 94:A9:90:2B:4A:CC): start bez błędów, karta SD i czas OK, przetwornik 3,97 mA.
- Płytka testowa ma zapisane starsze ustawienia (wyjścia, Modbus włączony, logi 365 dni) – nowe domyślne po ustawieniach fabrycznych z zachowaniem kalibracji.
- Do sprawdzenia na żywo: strona (widoki Logów, kasowanie automatyczne, opis API, usterka zbiorcza), miganie WY1–WY3 przy odłączonym przetworniku, tworzenie plików podsumowań po północy, kalibracja.
- W terenie: buzzer lampki przy obniżonej jasności, prąd wyjść MOSFET.
