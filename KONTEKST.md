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
- **Podpis aktualizacji (od 0.5.4):** sterownik przyjmuje przez stronę/API tylko pliki z podpisem ECDSA P-256 (`otasig.cpp`, klucz publiczny `ota_pub.h`, odcisk w pliku). Klucz prywatny: `~/.config/monitorup/ota_sign_key.pem` na serwerze agenta (poza repo, chmod 600) – **zgubienie = brak aktualizacji przez sieć** (zostaje USB). `build.sh` robi `build/MonitorUp_v<SW>.bin` (podpisany) – ten plik idzie na kartę i do OTA. Sprawdzenie: `python3 sign_fw.py --sprawdz plik.bin`.
- Wgranie przez stanowisko zdalne (Windows DESKTOP-IQTQJI5 przez WireGuard, `ssh esp-lab`, płytka na COM8): sam program pod `0x10000` – ustawienia (NVS) zostają:
  `scp 02_Monitor_App/build/02_Monitor_App.ino.bin esp-lab:C:/esp/fw/monitorup/` i `esptool --chip esp32s3 --port COM8 -b 921600 write-flash -z 0x10000 02_Monitor_App.ino.bin`. Pełny zestaw (pierwsze wgranie): `0x0` bootloader, `0x8000` partycje, `0xe000` boot_app0, `0x10000` program. Nie wgrywaj `merged.bin` (16 MB) – kasuje ustawienia.
- Log: `~/bin/esp-remote monitor --id COM8 -s 60` (po resecie pierwsze linie giną – natywne USB).
- Bez kabla: strona urządzenia → Ustawienia → System → Aktualizacja, plik `MonitorUp_v<SW>.bin` (podpisany).
- Strona sterownika (192.168.0.12) nie jest osiągalna z sieci stanowiska, dopóki stanowisko nie dostanie adresu 192.168.0.x (polecenie w karcie #139).

## Wydanie wersji (po każdej zmianie)
1. `FW_VERSION` w `hw.h`, `FW_NUM` w `modbus.cpp`, wpis w `FUNKCJE.md`, aktualizacja tego pliku, `README.md`/`MODBUS.md`/`API.md`, gdy dotyczy.
2. Kompilacja bez ostrzeżeń (`build.sh` podpisuje `build/MonitorUp_vX.Y.Z.bin`), `node --check` dla JS, commit na `dev`, **tag `vX.Y.Z`**, `git push origin dev --tags`.
3. Karta #139: załącznik `MonitorUp_vX.Y.Z.bin` – **podpisany** z `build/` (stare zostają), podmiana `MonitorUp_funkcje_i_wersje.md` (najpierw usuń stary załącznik `?type=file` i plik `Deck/…` przez WebDAV, inaczej Nextcloud dopisze „(2)”), komentarz z opisem wersji, aktualny opis karty (wersja, czy wgrana, lista ✋).

## Powrót do wcześniejszej wersji (git)
- Lista wersji: `git tag -l` (każda wersja ma tag `vX.Y.Z`).
- Podgląd całej wersji: `git switch --detach v0.4.5` (powrót: `git switch dev`).
- Jeden plik z wersji: `git checkout v0.4.5 -- 02_Monitor_App/outputs.cpp`.
- Różnice między wersjami: `git diff v0.4.5 v0.4.7 -- 02_Monitor_App/`.
- Wycofanie zmiany na `dev` bez przepisywania historii: `git revert <commit>`.
- Gotowe pliki `.bin` każdej wersji są też w załącznikach karty #139.

## Stan na 19.11.2026 (0.5.6-dev – NIEWYDANA, wgrana na płytkę testową 19.11 00:14 przez OTA z Pi masim)
Karta #177 (rejestr produkcji). Zawiera wszystko z 0.5.5-dev oraz:
- **Dane fabryczne** `fab.h/.cpp`: przestrzeń NVS `fab` (klucz `d` = `FabData`, `cal` = fabryczna tabela kalibracji prądu, `crt`/`key` zarezerwowane na mTLS). Nie kasują jej ustawienia fabryczne, DOWNLOAD, kopia ustawień ani OTA. Kopia numeru z CRC w EEPROM od adresu **232** (mapa EEPROM: 0–121 kalibracja zbiorników, 128–229 tabela prądu, 232–253 numer). `fabLoad()` przed `cfgLoad()`.
- **Polecenia USB** (`fabLoop()` w pętli): `PROV-INFO`, `PROVISION {json}`, `PROV-CAL {json}`, `PROV-MEAS`, `PROV-OUTTEST`; ponownie tylko `"serwis":true` + `sn_stary`. Hasła nigdy na port. Skrypt stanowiska: repo `Blazejeeek/rejestr-produkcji` (`tools/produkcja.py`).
- `accountDefaults()`/`netDefaults()`: przy danych fabrycznych hasło panelu = skrót z `fab`, hasło AP = `fab.apPass`; bez nich jak dotąd (admin/admin, monitor123). `cfg.mustChange` → sesja ma tylko odczyt (`auth()` w web.cpp), OTA zablokowana, nowe hasło ≠ startowe.
- Ustawienia fabryczne bez „zachowaj kalibrację” → `calRestoreFactory()` (tabela z `fab`); `calLoad()` sięga po `fab`, gdy brak NVS i EEPROM.
- Numer seryjny: `/api/v1/status` (`sn`, `fab`), pasek stanu i System na stronie, OLED (start, ekran sieci), Modbus IR 15–18 i 20–27 (MODBUS.md, CSV), MQTT `urzadzenie` (JSON sn/fw/hw/mac).
- Sprawdzone na płytce: start, `sn` = "", `fab` = "brak" (płytka niezaprogramowana). Programowanie przez USB czeka na podłączenie USB płytki do masim (separator USB) i pyserial na Pi.

## Stan na 17.11.2026 (0.5.5-dev – NIEWYDANA, wgrana na płytkę testową)

### Gdzie jesteśmy
- W kodzie `FW_VERSION "0.5.5-dev"`, `FW_NUM 55`. Wgrane 17.11 19:01 (OTA z Pi masim, plik `build/MonitorUp_v0.5.5-dev.bin`, podpisany). **Bez tagu, bez wydania.** Ostatni commit: b6948d1 (dev, lokalnie – push robi użytkownik: `git push origin dev`; agentowi tryb auto blokuje push).
- Uwaga `build.sh`: nazwa pliku z `FW_VERSION` – przy niezmienionej wersji nadpisuje podpisany plik wydania (17.11 nadpisał 0.5.4, przywrócony z karty #139). Zawsze najpierw podbij wersję (`-dev`).
- Próba 22 h na 0.5.4 (16.11 16:12 – 17.11 14:12): zaliczona (0 restartów, 0 błędów, heap stabilny) – komentarz w karcie #139.

### Co zawiera 0.5.5 (gotowe w kodzie)
1. **Sygnalizacja wg nakładki** (decyzje użytkownika 16–17.11): WY1 NORMA zielona; WY2 OSTRZEŻENIE pomarańczowa – ciągłe = poziom minimalny, przerywane 1×/s = awaria czujnika; WY3 ALARM czerwona – ciągłe = krytyczny, 1×/s = przepełnienie, 4×/s (`period` 250 ms) = nagły spadek. **Buzer WY4**: stany ALARM + awaria czujnika, w rytmie migającej kontrolki (`blinkMask` WY2|WY4 przy awarii, WY3|WY4 przy alarmie); przy minimalnym cicho. Strona (`ST`, OUTFIX, opisy) i OLED – nazwy jak na nakładce; **Modbus/MQTT/API bez zmian** (NORMALNY, ZA DUŻY, BŁĄD CZUJNIKA…).
2. **Logi:** „Pobierz pomiary (CSV)” = wszystkie pomiary z dnia/miesiąca/roku jednym plikiem (`/api/v1/logs/csv?from&to`, limit 400 dni), „Pobierz podsumowanie (CSV)” = dane dobowe/miesięczne; luki między dniami z danymi → wiersz `RRRR-MM-DD;brak danych`; `webBusyTick()` w web.cpp obsługuje alarmy i DI1 w trakcie wysyłania.
3. **Powiadomienia e-mail** (`mail.h/.cpp`, Ustawienia → Powiadomienia): własny klient SMTP (SSL/TLS 465, STARTTLS 587, bez szyfr.; `useBuiltinCACertBundle`), 5 odbiorców z własną maską zdarzeń (`MailKind` w config.h: krytyczny, przepełnienie, spadek, awaria czujnika, minimalny, powrót do normy, skasowanie, usterka zbiorcza, raport dzienny), ochrona przed zalewem (`mailGapMin`, 15), ponawianie do 24 h, test `POST /api/v1/mail/test`. Pola na końcu `struct Config`. **Nietestowane z prawdziwym serwerem.**

### Testy 0.5.5 na płytce (17.11)
- OK: normalny, minimalny, krytyczny, przepełnienie 1,00×/s, nagły spadek 4,00×/s, kasowanie, CSV dzień/miesiąc/rok (`stanowisko/csvtest.py`).
- 18.11: awaria czujnika OK (pomarańczowa + sygnalizator 1×/s), e-mail OK (SMTP użytkownika h54.seohost.pl:465, 2 odbiorców @globalsmart.pl; powiadomienia obecnie wyłączone w ustawieniach). Ujednolicone nazwy: pulpit (OUT1–5, DI1), Alarmy, dziennik zdarzeń (`statePanelName()`), e-mail.
- **Zasada (użytkownik 18.11): każda zmiana nazw/opisów ma być naniesiona wszędzie** – nakładka, strona (pulpit, ustawienia, opisy), OLED, dziennik zdarzeń, e-mail, instrukcja, karta katalogowa, wykaz komunikatów e-mail. Wyjątek: Modbus/MQTT/API/CSV (zgodność BMS) – tabela A.5 w instrukcji tłumaczy.
- **Zostało przed wydaniem:** wydanie `FW_VERSION "0.5.5"`, tag, podpisany bin do karty, PDF-y dokumentów.
- `sym.py` na Pi ma stary tekst „czerwona miga 2×/s” (tylko opis w skrypcie).

### Grafika i dokumentacja (gotowe, wersje robocze)
- Nakładka A4 `grafika/nakladka_gen.py` (ciemna, logo, NORMA/OSTRZEŻENIE/ALARM, legenda ciągłe/przerywane/szybkie, zbiornik leżący ze strefami, przycisk „KASOWANIE ALARMU”, otwory jak dotąd); wersja biała `nakladka_min_gen.py` (nieużywana). Animacja `grafika/animacja_gen.py` → `animacja_stanow.gif`.
- `dokumentacja/instrukcja/`: `instrukcja_obslugi.md` (0.2 + rozdz. 7.7 e-mail, załączniki A Modbus, B MQTT, C API), `karta_katalogowa.md`; PDF w stylu firmowym: `python3 build_pdf.py` (Chromium). Zasady: dokument dla klienta bez odwołań do plików repo; styl profesjonalny, zalecenia w bezokoliczniku, minimum tekstu.
- **[do uzupełnienia] w dokumentach:** dane producenta, pobór mocy, temperatura pracy, IP obudowy, kody zamówieniowe, schemat podłączenia przetwornika, potwierdzenie progów ppoż. (woda 90/95/98, paliwo 80/90/95).

### Stan płytki testowej (18.11 09:05)
- 0.5.5-dev (z poprawkami nazw), czujnik A4 odłączony – awaria czujnika (alarm skasowany przez agenta po teście). Ustawienia użytkownika: wysokość 200 cm, progi 10/20/100 %, gęstość ok. 840.

## Historia stanu – 16.11.2026 (0.5.4)
- SW **0.5.4** wgrane na płytkę testową przez API (OTA z Pi masim: `ssh masim 'fw-flash ota ~/fw/MonitorUp-X.Y.Z.ino.bin'`); płytka bez USB (pętla masy), stan: `ssh masim 'masim plytka'`. Ethernet bez kabla (DHCP), dostęp przez AP 192.168.4.1.
- Układ stały: jeden zbiornik 4–20 mA na A4, 150 Ω, gęstość cieczy, tabela kalibracji prądu, wyjścia WY1 POPRAWNY, WY2 MINIMALNY, WY3 KRYTYCZNY, WY4 buzer, WY5 przekaźnik usterki.
- WY3 i buzer WY4 migają razem wg rodzaju alarmu (`blinkMask`/`blinkPeriodMs` w zadaniu PWM, outputs.cpp): ciągle KRYT, 1000 ms ZA DUŻY, 500 ms SPADEK, 333 ms błąd czujnika.
- IN1 na stanowisku stale aktywne (`di` = 1) – do wyjaśnienia z użytkownikiem; od 0.5.2 stan IN1 po starcie nie wycisza buzera.
- Alarmy: `cfg.alarmMode` (0 własne, 1 ppoż. woda, 2 ppoż. paliwo) + `cfg.thrPct[3]`; progi cm liczone z % w `cfgAlarmToCm()` (config.cpp, wołane w cfgEnforceFixed i przy zapisie). `ALARM_PRESET` = progi stałe – do potwierdzenia przez użytkownika.
- Kalibracja jednym punktem: `POST /api/v1/calibrate {"point":"level","cm":…}` zmienia `offsetCm`.
- Zmiana przeliczania poziomu (kalibracja, gęstość, zakres, tabela prądu) → `statsLevelReset()` (stats.cpp): koniec tankowania, `refEndMin`, wstrzymanie spadku na dropMin+1 min. Wykrywanie w web.cpp przez `lvlSig()`.
- logRow (logger.cpp) sprawdza `SD.exists` pliku dnia – po usunięciu logów zakłada katalogi od nowa.
- Reset hasła awaryjny: DOWNLOAD (GPIO0) 10 s podczas pracy → admin/admin + sieć fabryczna (AP hasło monitor123), kalibracja zostaje.
- Nakładka A4: `grafika/nakladka_gen.py` → SVG/PDF/PNG (otwory 22,3 mm, rozstaw do dopasowania do obudowy).
- Testy: `TESTY.md` + `stanowisko/` (sym.py, blink.py, dropen.py, droptest.sh). 16.11 przeszły testy 1–5 i 8; rytmy WY3 zmierzone przez API (pole `out` = faktyczny stan wyjść): 1,00 i 1,99 Hz.
- Testy 16.11 (0.5.2): wszystkie alarmy + usterka czujnika (ręczne odłączenie A4, `fault.py`) – OK. Zostaje: test 7 (> 21 mA, potrzebne wyjście czynne), pliki podsumowań po północy.
- Ustawienia płytki testowej zmienione przez użytkownika 16.11 ok. 13:27: wysokość 200 cm, progi 10/20/100 %, gęstość prawdopodobnie 840.
