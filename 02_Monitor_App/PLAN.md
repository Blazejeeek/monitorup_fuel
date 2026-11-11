# Monitor App – plan urządzenia (KinCony B8M / ESP32-S3)

Wersja planu: 0.5 (10.11.2026), po uwagach GS (wyjścia, Modbus, MQTT, statystyki zużycia), do akceptacji przed kodowaniem.
Podstawa: `dokumentacja/KinCony_B8M.md` oraz program testowy `01_Test_v_0_01` (wszystkie układy płytki sprawdzone, wgrywanie przez `upload.ps1`).

---

## 0. Decyzje i otwarte pytania

### 0.1 Ustalone z GS (10.11.2026)

| Temat | Decyzja |
|---|---|
| Wejścia przetworników | 4–20 mA: **zbiornik ppoż. na A3**, zbiornik paliwa na **A4** (wejście i typ nadal wybieralne w ustawieniach) |
| Progi alarmowe | od dołu: KRYTYCZNY < MINIMALNY < NORMALNY < ZA DUŻY, ustawiane **w cm** |
| Przechowywanie ustawień | NVS + kopia kalibracji i progów w EEPROM 24C02, pomiary na SD |
| Wyjścia MOSFET w 1.0 | syrena, lampka z buzzerem, alarm zbiorczy, z regulacją jasności (PWM) i kasowaniem alarmu wejściem DI1 (sekcja 11) |
| W wersji 1.0 | OTA przez wgranie pliku na stronie, kopia ustawień (pobierz / wgraj), watchdog, reset hasła przyciskiem, **MQTT**, **Modbus TCP z mapą rejestrów** (sekcje 12–13) |
| Statystyki (dopisane przez GS) | dzienne i średnie zużycie, prognoza „wystarczy na X dni”, alarm nagłego spadku (wyciek/kradzież) – w wersji 1.0 (sekcja 14) |
| Litry | w wersji 1.0 jako opcja włączana w ustawieniach przetwornika; zbiornik paliwa to **walec leżący**, wymiary (średnica, długość) wpisywane w konfiguracji (sekcja 5.1) |
| API | zamknięte: dostęp tylko z sesją strony albo z **tokenem API** (Bearer) generowanym i odwoływanym w ustawieniach (sekcja 9.6) |
| Test wyjść przy starcie | OK, syrena może zawyć na 0,8 s przy włączeniu zasilania |
| Start urządzenia | duże logo MONITOR APP na OLED przez 5 s z adresem www.monitorapp.pl, w tym czasie test wyjść 1–6 po kolei (sekcja 10 i 11.4) |

### 0.2 Nadal otwarte

| # | Pytanie | Założenie w planie |
|---|---|---|
| A | **Lampka z buzzerem i PWM** – wyjścia B8M idą przez ekspander I2C, nie mają sprzętowego PWM, a obniżenie napięcia impulsami nie gwarantuje, że buzzer zamilknie (szczegóły w 11.2). Jaki to model lampki i czy ma osobny przewód buzzera? | Programowy PWM na wyjściu + do sprawdzenia na prawdziwej lampce. Rozwiązanie pewne: lampka z osobnym przewodem buzzera (dwa wyjścia) albo sprzętowy PWM z GPIO13/14/21 przez zewnętrzny moduł MOSFET. |
| D | Model sond (zakres, zasilanie pętli 12/24 V). | Sonda hydrostatyczna 4–20 mA zasilana z 12/24 V płytki. |
| E | Interwał i czas przechowywania logów. | Co 5 min (1–60), kasowanie po 365 dniach lub przy < 10% wolnego. |
| F | Access Point. | Zawsze włączony, w ustawieniach: zawsze / awaryjnie / wyłączony. |

---

## 1. Cel i zakres

Urządzenie na szynę DIN mierzy poziom w dwóch zbiornikach (ppoż. i paliwo), pokazuje go na OLED i w przeglądarce, pilnuje progów alarmowych i zapisuje historię na kartę SD. Dostęp przez Ethernet, WiFi (klient) i własny Access Point, chroniony loginem i hasłem.

---

## 2. Sprzęt i wykorzystanie płytki

| Układ | Rola w Monitor App |
|---|---|
| ADS1115 @0x48 | pomiar przetworników (A3, A4, opcjonalnie A1/A2) |
| OLED SSD1306 @0x3C | ekran poziomu |
| DS3231 @0x68 | zegar podtrzymywany baterią (czas w logach bez sieci) |
| EEPROM 24C02 @0x50 | kopia zapasowa kalibracji i progów (sekcja 8) |
| Karta SD (SPI) | logi CSV, dziennik zdarzeń |
| W5500 | Ethernet |
| WiFi ESP32-S3 | klient WiFi + Access Point |
| PCF8575 @0x22 | wyjścia MOSFET OUT1–8 (syrena, lampka z buzzerem, alarm zbiorczy, programowy PWM), wejście DI1 = kasowanie alarmu, DI2–8 wolne |
| Przycisk Download (GPIO0) | długie wciśnięcie = reset ustawień sieci i hasła |
| DS18B20 (1-Wire) | opcjonalnie temperatura |

Ważne z testu: wszystkie układy I2C siedzą na jednej magistrali (GPIO8/18), więc dostęp do niej będzie chroniony muteksem (pomiar, OLED, RTC, EEPROM i PCF nie mogą sobie wchodzić w drogę).

**Partycje flash:** obecny układ `app3M_fat9M_16MB` ma jedną partycję programu, więc nie pozwala na aktualizację przez WWW (OTA). Proponuję własny `partitions.csv`: 2 × 3 MB program (OTA) + ok. 9,9 MB LittleFS na pliki strony. Rdzeń, FQBN i `upload.ps1` zostają jak w teście.

---

## 3. Architektura oprogramowania

Arduino (rdzeń ESP32 3.3.2), kod podzielony na moduły:

| Moduł | Zadanie |
|---|---|
| `config` | ustawienia: NVS + kopia w EEPROM 24C02, wartości domyślne, walidacja, eksport/import JSON |
| `sensors` | odczyt ADS1115, uśrednianie 2 s, przeliczenie na mA → cm → % → litry, wykrycie awarii czujnika |
| `alarms` | porównanie z progami, histereza, opóźnienie, stany, sterowanie wyjściami |
| `logger` | dzienne pliki CSV na SD, dziennik zdarzeń, rotacja, odczyt dla wykresu |
| `net` | Ethernet, WiFi STA, AP, DHCP/statyczny, skanowanie, mDNS `monitor.local` |
| `timesync` | DS3231 jako źródło czasu, NTP, ustawianie z przeglądarki, strefa czasowa |
| `web` | serwer HTTP, logowanie i sesje, API JSON, pliki strony z LittleFS |
| `display` | ekrany OLED |
| `outputs` | MOSFET i DI przez PCF8575, programowy PWM, test startowy, kasowanie DI1 |
| `modbus` | serwer Modbus TCP (port 502) wg mapy z sekcji 12 |
| `mqtt` | klient MQTT + autodiscovery Home Assistant |
| `stats` | zużycie dzienne i średnie, tankowania, prognoza, alarm nagłego spadku |

**Zadania (FreeRTOS):**
- pomiar: co 100 ms (osobne zadanie, rdzeń 1), nie blokuje go serwer WWW,
- PWM wyjść: osobne zadanie o najwyższym priorytecie (ok. 1 ms), wspólny muteks I2C,
- logika (alarmy, OLED, logi, czas): pętla główna,
- WWW: asynchronicznie (ESPAsyncWebServer).

**Biblioteki:** ESPAsyncWebServer + AsyncTCP (wersje ESP32Async), ArduinoJson, PubSubClient (MQTT), Modbus TCP (własna lekka implementacja na AsyncTCP albo biblioteka eModbus), U8g2 (ładne czcionki na OLED, zastąpi prosty `oled.h`), istniejące SD, ETH, WiFi, Preferences, LittleFS. Wykres w przeglądarce: uPlot (ok. 45 kB) trzymany w pamięci urządzenia, żeby działał bez internetu (np. w trybie AP).

---

## 4. Pomiar

1. ADS1115 w trybie pojedynczym, 128 próbek/s, odczyt każdego aktywnego kanału co 100 ms → ok. 20 próbek na kanał w 2 s.
2. **Okno 2 s:** z ostatnich 20 próbek odrzucane są skrajne (np. 2 najmniejsze i 2 największe), z reszty liczona średnia. Wynik odświeżany co 1 s, więc wartość nie skacze, a reaguje bez dużego opóźnienia.
3. Opcjonalnie dodatkowe wygładzanie (filtr wykładniczy, ustawiany 0–100%) dla falującej cieczy.
4. Przeliczenie: napięcie → prąd (mA, na podstawie kalibracji) → poziom cm → % zbiornika → litry (gdy włączone przeliczanie, sekcja 5.1).
5. **Awaria czujnika** (wg NAMUR NE43): prąd < 3,6 mA = przerwa w obwodzie, > 21 mA = zwarcie/przekroczenie. Pokazywane jako „BŁĄD CZUJNIKA” i osobny alarm, a w logu pusta wartość zamiast fałszywego 0 cm.
6. Wzmocnienie ADS1115 dobierane do bocznika: zaczynamy od ±6,144 V jak w teście, po zmierzeniu napięcia przy 20 mA można przejść na ±4,096 V (lepsza rozdzielczość).

---

## 5. Kalibracja przetworników

Dla każdego czujnika (strona Ustawienia → Przetworniki):

| Pole | Opis |
|---|---|
| Nazwa | np. „Zbiornik ppoż.” |
| Wejście / typ | A1–A4, 4–20 mA lub 0–5 V |
| Aktywny | wł./wył. |
| **Punkt MIN** | odczyt surowy + poziom w cm (zwykle 0 cm = pusty, ok. 4 mA) |
| **Punkt MAX** | odczyt surowy + poziom w cm (pełny zakres albo znany poziom) |
| Przesunięcie montażowe | o ile cm sonda wisi nad dnem (dodawane do wyniku) |
| Wysokość zbiornika | do liczenia % |
| Wygładzanie | 0–100% |
| Przeliczanie na litry | wł./wył. + kształt i wymiary (sekcja 5.1) |

Kalibrację robi się na dwa sposoby: przycisk **„Zapisz bieżący odczyt jako MIN/MAX”** (przy pustym/znanym poziomie) albo ręczne wpisanie wartości (np. z karty sondy: 4 mA = 0 cm, 20 mA = 300 cm). Na stronie na żywo widać napięcie, prąd i wynik. Zapis tylko po kliknięciu „Zapisz”, z potwierdzeniem.


### 5.1 Przeliczanie na litry (opcja)

Włączane osobno dla każdego zbiornika. Gdy wyłączone, wszędzie pokazywane są tylko cm i %.

| Kształt | Wymiary do wpisania | Wzór (h = poziom) |
|---|---|---|
| **Walec leżący** (zbiornik paliwa) | średnica wewnętrzna D [cm], długość L [cm] | `V = L · (r²·acos((r−h)/r) − (r−h)·√(2rh − h²))`, r = D/2 |
| Walec stojący | średnica D, wysokość | `V = π·r²·h` |
| Prostopadłościan | długość, szerokość, wysokość | `V = a·b·h` |
| Tabela (dla zbiorników z dennicami, nieregularnych) | do 20 par „cm → litry” z tabeli producenta lub z wlewania | interpolacja liniowa |

- Wynik w litrach (1000 cm³ = 1 l), pokazywany jako pojemność całkowita, aktualna ilość i wolne miejsce do dolania.
- Na stronie od razu widać wyliczoną pojemność całkowitą, co pozwala sprawdzić wpisane wymiary z tabliczką zbiornika.
- Dla walca leżącego wysokość zbiornika = średnica, więc % jest liczony z objętości, a nie z wysokości (przy leżącym walcu to istotna różnica).
- Gdy przeliczanie jest włączone: progi alarmowe nadal w cm (jak ustalono), a zużycie, średnie, tankowania i prognoza w **litrach** (i dodatkowo w cm).

---

## 6. Progi alarmowe

Osobno dla każdego zbiornika, **w cm** (procent liczony i pokazywany pomocniczo):

| Stan | Warunek | Kolor |
|---|---|---|
| KRYTYCZNY | poziom ≤ próg krytyczny | czerwony, miga |
| MINIMALNY | poziom ≤ próg minimalny | pomarańczowy |
| NORMALNY | pomiędzy minimalnym a „za dużym” | zielony |
| ZA DUŻY | poziom ≥ próg przepełnienia | niebieski/fioletowy, miga |
| BŁĄD CZUJNIKA | prąd poza 3,6–21 mA | szary |
| NAGŁY SPADEK | szybki spadek poziomu (sekcja 14.4) | czerwony, miga |

- **Histereza** (np. 2 cm), żeby alarm nie migał na granicy progu.
- **Opóźnienie** (np. 10 s), żeby fala nie wywoływała alarmu.
- Walidacja: krytyczny < minimalny < za duży ≤ wysokość zbiornika.
- Każda zmiana stanu trafia do dziennika zdarzeń, steruje wyjściami (sekcja 11), MQTT i rejestrami Modbus.

---

## 7. Logi na karcie SD

**Pliki dzienne:** `/logs/2026/11/2026-11-10.csv`, zapis co X min (ustawienie 1–60, domyślnie 5) wartości uśrednionej, plus dodatkowy wpis przy każdej zmianie stanu alarmu.

Format (separator `;`, przecinek dziesiętny, więc plik otwiera się od razu w polskim Excelu):

```
data;czas;ppoz_cm;ppoz_proc;ppoz_l;ppoz_mA;ppoz_stan;paliwo_cm;paliwo_proc;paliwo_l;paliwo_mA;paliwo_stan;alarm_skwitowany
2026-11-10;12:05:00;182,4;60,8;;13,73;NORMALNY;95,1;47,6;4520;9,07;NORMALNY;0
```

**Dziennik zdarzeń:** `/events/2026-11.csv` – start urządzenia, alarmy, kasowanie alarmu (ze źródłem), zmiany ustawień, logowania, synchronizacje czasu, błędy SD/czujnika.

**Bezpieczeństwo danych:** zapis z dopisaniem i `flush()` po każdej linii, brak karty nie zatrzymuje urządzenia (komunikat na stronie i OLED), ostatnie 24 h trzymane też w PSRAM (8 MB), więc wykres „ostatnia doba” działa nawet bez karty.

**Rotacja:** kasowanie plików starszych niż N dni (domyślnie 365) lub gdy wolne < 10%.

---

## 8. Gdzie zapisywać ustawienia

| Dane | Miejsce | Dlaczego |
|---|---|---|
| Wszystkie ustawienia (sieć, hasło, NTP, kalibracja, progi, logi) | **NVS** (pamięć flash ESP32, biblioteka Preferences) | duża pojemność, rozkładanie zapisów (wear leveling), wygodna obsługa |
| Kalibracja i progi (kopia zapasowa) | **EEPROM 24C02** (256 B) z sumą kontrolną CRC | przetrwa wgranie programu z pełnym kasowaniem flash, więc po aktualizacji nie trzeba kalibrować od nowa |
| Hasło | NVS jako skrót SHA-256 z solą | hasło nie jest przechowywane jawnie |
| Pomiary i zdarzenia | **karta SD** | duże ilości danych |
| Strona WWW (HTML/CSS/JS/logo) | **LittleFS** w flash | aktualizowana razem z programem |

Przy starcie: jeśli NVS jest pusty lub uszkodzony, a EEPROM ma poprawną kopię (CRC OK), kalibracja i progi są odtwarzane z EEPROM. 256 B wystarcza: 2 czujniki × (kalibracja + progi) to ok. 2 × 48 B.

Zapis tylko po zmianie ustawień, nie przy każdym pomiarze, więc trwałość pamięci (EEPROM ok. 1 mln zapisów) nie jest problemem.

---

## 9. Interfejs WWW

Wspólny wygląd: ciemny lub jasny motyw, logo **Monitor App** (ikona zbiornika z falą + napis, SVG), działa na telefonie i komputerze, polski język, wszystkie pliki w urządzeniu (bez internetu).

### 9.1 Logowanie
- Adres: `http://<IP>` lub `http://monitor.local` → ekran logowania z logo, polami „Login” i „Hasło”.
- Domyślnie `admin` / `admin`, przy pierwszym logowaniu wymuszona zmiana hasła.
- Sesja: losowy token w ciasteczku, wygasa po 30 min bezczynności (ustawialne), przycisk „Wyloguj”.
- Ochrona: po 5 błędnych próbach blokada na 5 min.

### 9.2 Pulpit (po zalogowaniu)
Dwie duże karty: **Zbiornik ppoż.** i **Zbiornik paliwa**, na każdej:
- grafika zbiornika z animowanym poziomem cieczy, kolor wg stanu alarmu,
- poziom w cm (duża liczba), %, a przy włączonym przeliczaniu litry i wolne miejsce,
- stan: NORMALNY / MINIMALNY / KRYTYCZNY / ZA DUŻY / BŁĄD CZUJNIKA,
- znaczniki progów na grafice,
- trend (strzałka ↑ ↓ → na podstawie ostatniej godziny) i mini wykres 24 h,
- prąd przetwornika (mA) i czas ostatniego pomiaru,
- dla zbiornika ze statystykami: zużycie dziś, średnie z 7 dni, **„wystarczy na ok. X dni (do dd.mm)”**, ostatnie tankowanie.

Nad kartami pasek alarmu (gdy jest): stan, od kiedy, przycisk **„Kasuj alarm”**. Obok kafelki wyjść OUT1–8 (stan, jasność).

Pasek stanu na dole: data i godzina, WiFi (sieć, siła sygnału, IP), Ethernet (łącze, IP), karta SD (wolne miejsce), czas pracy, wersja programu. Odświeżanie co 1–2 s.

### 9.3 Logi
- Wybór dnia (kalendarz) albo zakresu: doba / 7 dni / 30 dni.
- **Wykres** poziomu obu zbiorników z liniami progów (powiększanie, przesuwanie, podpowiedź wartości po najechaniu).
- Tabela odczytów pod wykresem.
- Pobieranie pliku CSV wybranego dnia lub zakresu.
- Wykres słupkowy zużycia dziennego (30 dni / rok) ze znacznikami tankowań.
- Lista plików na karcie, wolne miejsce, dziennik zdarzeń.

### 9.4 Ustawienia (zakładki)

| Zakładka | Zawartość |
|---|---|
| **Konto** | zmiana loginu i hasła (stare + nowe ×2), czas wygasania sesji |
| **Access Point** | włącz / tylko awaryjnie / wyłącz, SSID, hasło (min. 8 znaków), kanał, ukryta sieć; stały adres 192.168.4.1 |
| **WiFi** | przycisk **Szukaj sieci** (lista: nazwa, siła sygnału, zabezpieczenie), wybór sieci, hasło, **Połącz**, stan połączenia; **DHCP lub statyczny** (IP, maska, brama, DNS 1/2) |
| **Ethernet** | włącz/wyłącz, stan łącza i prędkość, MAC, **DHCP lub statyczny** (IP, maska, brama, DNS) |
| **Przetworniki** | sekcja 5, podgląd na żywo, przyciski kalibracji |
| **Alarmy** | sekcja 6, progi, histereza, opóźnienie, przypomnienie |
| **Wyjścia** | sekcja 11: funkcja każdego wyjścia, jasność PWM przed/po skasowaniu, tryb przerywany, test startowy, sterowanie ręczne |
| **Statystyki** | sekcja 14.6 |
| **API** | sekcja 9.6: tokeny integracji (generowanie, lista, odwołanie), dokumentacja API |
| **MQTT** | sekcja 13 |
| **Modbus** | sekcja 12: wł./wył., port, Unit ID, zapis wł./wył., dozwolone IP, pobranie mapy rejestrów |
| **Data i czas** | bieżący czas urządzenia; **NTP wł./wył.**, serwer (domyślnie `pool.ntp.org`, drugi zapasowy), co ile synchronizować, strefa czasowa (Polska z czasem letnim); przycisk **„Ustaw czas z przeglądarki”**; ręczne wpisanie daty |
| **Logi** | interwał zapisu, czas przechowywania, formatowanie karty |
| **System** | nazwa urządzenia, restart, przywrócenie ustawień fabrycznych, kopia ustawień (pobierz / wgraj JSON), aktualizacja programu (OTA), informacje (wersja, wolna pamięć, temperatura DS3231) |

Zmiana sieci: przed zapisaniem okno z ostrzeżeniem, a po zapisie strona podaje nowy adres. Jeśli nowe ustawienia WiFi nie zadziałają w 60 s, urządzenie wraca do poprzednich (AP pozostaje dostępny).

Uwaga techniczna: przy połączeniu z WiFi Access Point przełącza się na kanał routera, więc telefon podłączony do AP może się na chwilę rozłączyć. To normalne zachowanie ESP32.

### 9.5 API (dla strony i integracji, wersjonowane jako `/api/v1/...`)

| Metoda | Adres | Opis |
|---|---|---|
| POST | `/api/login`, `/api/logout` | sesja |
| GET | `/api/status` | poziomy, stany, sieć, czas (JSON) |
| GET/POST | `/api/config/{sekcja}` | odczyt/zapis ustawień |
| GET | `/api/wifi/scan` | wyniki skanowania |
| POST | `/api/calibrate` | zapis punktu MIN/MAX z bieżącego odczytu |
| POST | `/api/time` | ustawienie czasu (z przeglądarki) |
| GET | `/api/logs?from=&to=` | dane do wykresu (z redukcją punktów dla dłuższych zakresów) |
| GET | `/api/logs/file?d=2026-11-10` | pobranie CSV |
| GET | `/api/events` | dziennik zdarzeń |
| POST | `/api/ota` | wgranie nowego programu |
| POST | `/api/alarm/ack` | kasowanie alarmu |
| GET/POST | `/api/outputs` | stan i sterowanie ręczne wyjść |
| GET/POST | `/api/backup` | pobranie / wgranie kopii ustawień |


### 9.6 API dla integracji i tokeny

Domyślnie API **nie jest otwarte**: każde zapytanie wymaga albo zalogowanej sesji strony (ciasteczko), albo **tokenu API**. Tokeny są osobne od loginu i hasła do strony, więc system zewnętrzny nie zna hasła administratora, a token można w każdej chwili odwołać bez zmiany hasła.

**Zarządzanie (Ustawienia → API, tylko dla zalogowanego administratora):**
- **Generuj token:** nazwa (np. „SCADA kotłownia”), uprawnienia, opcjonalnie data ważności i dozwolone adresy IP. Token (32 losowe bajty, zapis w postaci tekstowej np. `ma_7Fq…`) jest pokazywany **tylko raz**, z przyciskiem „Kopiuj”.
- W urządzeniu przechowywany jest tylko skrót SHA-256 tokenu (NVS), więc nawet odczyt pamięci go nie ujawnia.
- Lista tokenów: nazwa, uprawnienia, data utworzenia, ostatnie użycie i z jakiego IP, ważność; przyciski **Odwołaj** (natychmiast) i **Odnów** (nowy token, stary przestaje działać).
- Do 8 tokenów jednocześnie.

**Uprawnienia tokenu:**

| Poziom | Dostęp |
|---|---|
| Odczyt | `/api/v1/status`, `/api/v1/tanks`, `/api/v1/stats`, `/api/v1/logs`, `/api/v1/events` |
| Odczyt + sterowanie | jak wyżej + kasowanie alarmu, sterowanie wyjściami „ręcznymi”, wstrzymanie alarmu nagłego spadku |
| Pełny (konfiguracja) | jak wyżej + zmiana progów i ustawień (bez zmiany hasła, tokenów i OTA, które zawsze wymagają zalogowania na stronie) |

**Użycie:**

```
GET /api/v1/status HTTP/1.1
Host: 192.168.1.50
Authorization: Bearer ma_7Fq...
```

- Token tylko w nagłówku `Authorization: Bearer` (nie w adresie URL, żeby nie trafiał do logów i historii przeglądarki).
- Odpowiedzi w JSON, błędy: 401 brak/zły token, 403 brak uprawnień, 429 za dużo zapytań.
- Limit: ok. 10 zapytań/s na token, 5 błędnych tokenów z jednego IP = blokada IP na 5 min.
- Każde użycie tokenu do sterowania lub zmiany ustawień trafia do dziennika zdarzeń z nazwą tokenu.
- Dokumentacja API (lista adresów, przykładowe odpowiedzi, przykład curl) do pobrania ze strony Ustawienia → API oraz w pliku `API.md`.
- Opcja „Odczyt bez tokenu z sieci lokalnej” (domyślnie wyłączona) dla prostych integracji, np. wyświetlacza w biurze.

Uwaga: połączenie jest po HTTP, więc token przesyłany jest jawnie w sieci lokalnej (jak w Modbus i MQTT). Dlatego urządzenie nie powinno być wystawione do internetu; zdalny dostęp najlepiej przez VPN.

---

## 10. Wyświetlacz OLED (128 × 64)

**Start (5 s):** duże logo **MONITOR APP** (ikona zbiornika z falą + napis na całą szerokość), pod spodem mniejszym drukiem **www.monitorapp.pl** i wersja programu. W tym samym czasie trwa test wyjść 1–6 (sekcja 11.4), a na dole ekranu widać, które wyjście jest właśnie załączone.

**Ekran zbiornika** (przełączane co 5 s: ppoż. → paliwo → sieć):

```
┌────────────────────────────┐
│ MONITOR APP     📶 ⇄  ⚠    │  pasek: nazwa, ikony WiFi/ETH/SD/alarm
│────────────────────────────│
│ Zbiornik ppoż.             │
│      182,4 cm        ▐██▌  │  duża liczba + pionowy słupek zbiornika
│ 61%  13,7 mA NORMALNY ▐██▌ │
│────────────────────────────│
│ 10.11.2026  12:05:31  SD✓  │  mała data/godzina
└────────────────────────────┘
```

**Ekran sieci:** IP Ethernet, IP WiFi + siła sygnału, stan AP (SSID, 192.168.4.1), stan MQTT i liczba połączeń Modbus.

**Alarm:** przy stanie KRYTYCZNY lub ZA DUŻY ekran tego zbiornika zostaje na stałe i miga inwersją, napis stanu dużymi literami. Po skasowaniu alarmu przestaje migać, ale zostaje oznaczenie „SKWITOWANY”. Błąd czujnika: „BŁĄD CZUJNIKA” zamiast liczby.

Ochrona OLED przed wypaleniem: po 10 min przyciemnienie, co minutę lekkie przesunięcie obrazu o 1–2 piksele (ustawialne).

---

## 11. Wyjścia alarmowe (MOSFET), PWM i kasowanie alarmu

### 11.1 Przypisanie wyjść (domyślne, zmieniane w Ustawieniach → Wyjścia)

| Wyjście | Funkcja domyślna | Kiedy działa |
|---|---|---|
| OUT1 | **Syrena** | alarm aktywny i nieskwitowany (KRYTYCZNY lub ZA DUŻY w dowolnym zbiorniku) |
| OUT2 | **Lampka z buzzerem** | alarm aktywny: 100% przed skasowaniem, obniżona jasność po skasowaniu (sekcja 11.3) |
| OUT3 | **Alarm zbiorczy** (do centrali ppoż. / BMS) | każdy stan inny niż NORMALNY, także MINIMALNY i błąd czujnika, do powrotu do normy |
| OUT4–OUT6 | wolne (np. osobne lampki dla zbiorników, ostrzeżenie MINIMALNY) | wg ustawień |
| OUT7–OUT8 | wolne, **pomijane w teście startowym** (miejsce np. na pompę) | wg ustawień |

Dla każdego wyjścia ustawia się: funkcję (wyłączone / ręczne / syrena / lampka / alarm zbiorczy / stan konkretnego zbiornika), które stany je włączają, tryb (ciągły / przerywany z czasem wł./wył.), jasność PWM przed i po skasowaniu (0–100%) oraz czy ma się wyłączyć po skasowaniu. Na stronie jest też sterowanie ręczne do testów.

### 11.2 PWM na wyjściach – ograniczenia płytki (ważne)

Wyjścia MOSFET B8M nie są podpięte do pinów ESP32, tylko do ekspandera PCF8575 na I2C. Nie ma więc sprzętowego PWM, a każdą zmianę stanu trzeba wysłać po magistrali.

- **Programowy PWM przez I2C jest możliwy, ale zgrubny:** ok. 100 Hz i 10 poziomów (10%, 20% … 100%). To ok. 1000 zapisów do PCF8575 na sekundę, czyli ok. 10% czasu magistrali. Inne układy na tej samej magistrali (zwłaszcza OLED) mogą powodować drobne migotanie, dlatego odświeżanie OLED będzie dzielone na małe kawałki, a PWM dostanie najwyższy priorytet.
- **Lampka z wbudowanym buzzerem:** PWM nie obniża napięcia, tylko szybko je włącza i wyłącza. Lampka świeci słabiej, ale elektronika buzzera w każdym impulsie dostaje pełne 12/24 V, więc buzzer może cicho terkotać z częstotliwością PWM zamiast całkiem zamilknąć. Zależy to od konkretnej lampki i trzeba to sprawdzić na prawdziwym egzemplarzu.
- **Rozwiązania pewne**, jeśli test wypadnie źle:
  1. lampka z osobnym przewodem buzzera (3 żyły: wspólny, światło, dźwięk) → światło na OUT2, buzzer na OUT4, kasowanie wyłącza tylko buzzer; PWM nie jest wtedy potrzebny,
  2. sprzętowy PWM (kilka kHz, płynna regulacja) z wolnego GPIO13/14/21 przez zewnętrzny moduł MOSFET lub tranzystor (GPIO 3,3 V bez zabezpieczeń, więc tylko przez moduł),
  3. prawdziwe obniżenie napięcia dla lampki (np. przetwornica/stabilizator na drugim torze), przełączane wyjściem MOSFET.

W planie: programowy PWM na wyjściach PCF8575 w wersji 1.0 (jasność 0–100% w krokach 10%), a w ustawieniach możliwość przypisania funkcji „lampka” do GPIO13/14/21 ze sprzętowym PWM, gdyby trzeba było przejść na wariant 2.

### 11.3 Logika alarmu i kasowanie wejściem DI1

| Stan | Syrena (OUT1) | Lampka (OUT2) | Alarm zbiorczy (OUT3) |
|---|---|---|---|
| NORMALNY | wył. | wył. | wył. |
| MINIMALNY (ostrzeżenie) | wył. | wył. lub mruga słabo (ustawialne) | wł. |
| KRYTYCZNY / ZA DUŻY / NAGŁY SPADEK, **nieskwitowany** | wł. | **100%** (światło + buzzer) | wł. |
| KRYTYCZNY / ZA DUŻY / NAGŁY SPADEK, **skwitowany** | wył. | **obniżona jasność**, np. 40% (światło bez buzzera) | wł. |
| powrót do NORMALNEGO | wył. | wył. | wył. |

- **Kasowanie (kwitowanie):** zwarcie wejścia **DI1** (przycisk na elewacji/szafie), przycisk na stronie WWW, cewka Modbus lub komenda MQTT.
- Wejście DI1 reaguje na zbocze (przytrzymanie przycisku nie blokuje kolejnych alarmów), z filtrem drgań styków 50 ms.
- Nowy, gorszy stan (np. drugi zbiornik wchodzi w alarm albo z MINIMALNEGO robi się KRYTYCZNY) **ponownie uruchamia** syrenę i pełną jasność.
- Opcjonalnie: przypomnienie, czyli ponowne uruchomienie syreny po X min, jeśli alarm nadal trwa (domyślnie wyłączone).
- Każde kasowanie trafia do dziennika zdarzeń ze źródłem (DI1 / WWW / Modbus / MQTT).

### 11.4 Test wyjść przy starcie

- Po włączeniu zasilania, w czasie wyświetlania logo, wyjścia **OUT1 → OUT6 załączają się po kolei**, każde na 0,8 s (razem ok. 5 s), potem wszystkie wyłączone i normalna praca.
- W ustawieniach: test wł./wył., zakres wyjść i czas na wyjście.
- Uwaga: test naprawdę załącza to, co jest podłączone (syrena zawyje na 0,8 s). Pompy i zawory podłączaj na OUT7–OUT8 albo wyłącz dla nich test.
- Po restarcie z watchdoga lub po OTA test jest pomijany, żeby syrena nie odzywała się przy każdym restarcie programowym (ustawialne).

---

## 12. Modbus TCP

### 12.1 Parametry

| Parametr | Wartość |
|---|---|
| Port | 502 (zmienny) |
| Adres urządzenia (Unit ID) | 1 (zmienny, serwer odpowiada też na 0 i 255) |
| Interfejsy | Ethernet i WiFi |
| Jednoczesne połączenia | do 4 |
| Funkcje | 01 cewki, 02 wejścia dyskretne, 03 rejestry holding, 04 rejestry wejściowe, 05 zapis cewki, 06 zapis rejestru, 15 zapis cewek, 16 zapis rejestrów |
| Adresowanie | od 0 (adres 0 = 30001 / 40001 w notacji SCADA) |
| Typy | INT16/UINT16; UINT32 i FLOAT32 w dwóch rejestrach, starsze słowo pierwsze (big-endian, „ABCD”) |
| Bezpieczeństwo | Modbus nie ma haseł, więc **zapis domyślnie wyłączony** (tylko odczyt); w ustawieniach: włącz zapis, lista dozwolonych adresów IP |

Wartości stałoprzecinkowe: „×10” oznacza, że 1824 = 182,4 cm. Brak pomiaru (błąd czujnika, kanał wyłączony) = **-32768 (0x8000)** w rejestrach INT16 i NaN w FLOAT32.

### 12.2 Rejestry wejściowe (FC04, tylko odczyt)

**Urządzenie**

| Adres | SCADA | Nazwa | Typ | Jedn. / opis |
|---|---|---|---|---|
| 0 | 30001 | Wersja programu | UINT16 | 100 = 1.00 |
| 1 | 30002 | Status urządzenia | UINT16 bity | b0 SD OK, b1 ADS1115 OK, b2 RTC OK, b3 czas zsynchronizowany, b4 Ethernet link, b5 WiFi połączone, b6 MQTT połączone, b7 alarm aktywny, b8 alarm skwitowany, b9 test startowy w toku |
| 2–3 | 30003 | Czas pracy | UINT32 | s |
| 4 | 30005 | Rok | UINT16 | czas lokalny |
| 5 | 30006 | Miesiąc | UINT16 | 1–12 |
| 6 | 30007 | Dzień | UINT16 | 1–31 |
| 7 | 30008 | Godzina | UINT16 | 0–23 |
| 8 | 30009 | Minuta | UINT16 | 0–59 |
| 9 | 30010 | Sekunda | UINT16 | 0–59 |
| 10 | 30011 | Alarm zbiorczy | UINT16 | najgorszy stan z obu zbiorników (kody jak niżej) |
| 11 | 30012 | Stan wyjść OUT1–8 | UINT16 bity | b0 = OUT1 … b7 = OUT8 |
| 12 | 30013 | Stan wejść DI1–8 | UINT16 bity | b0 = DI1 … b7 = DI8 |
| 13 | 30014 | Wolne miejsce SD | UINT16 | MB (max 65535) |
| 14 | 30015 | Temperatura DS3231 | INT16 | °C ×10 |

**Zbiornik 1 (ppoż.) – adresy 100–129, zbiornik 2 (paliwo) – adresy 200–229** (ten sam układ, przesunięcie +100)

| Adres Z1 | Adres Z2 | Nazwa | Typ | Jedn. / opis |
|---|---|---|---|---|
| 100 | 200 | Poziom | INT16 | cm ×10 |
| 101 | 201 | Wypełnienie | INT16 | % ×10 |
| 102 | 202 | Prąd przetwornika | INT16 | mA ×100 |
| 103 | 203 | Stan alarmu | UINT16 | 0 NORMALNY, 1 MINIMALNY, 2 KRYTYCZNY, 3 ZA DUŻY, 4 BŁĄD CZUJNIKA, 5 kanał wyłączony, 6 NAGŁY SPADEK |
| 104 | 204 | Flagi | UINT16 bity | b0 alarm skwitowany, b1 pomiar ważny, b2 kalibracja ustawiona |
| 105 | 205 | Trend | INT16 | cm/h ×10 (ujemny = spada) |
| 106 | 206 | Surowy odczyt ADC | INT16 | jednostki ADS1115 |
| 107 | 207 | Napięcie na wejściu | INT16 | mV |
| 108–109 | 208–209 | Poziom | FLOAT32 | cm |
| 110–111 | 210–211 | Wypełnienie | FLOAT32 | % |
| 112–113 | 212–213 | Prąd | FLOAT32 | mA |
| 114 | 214 | Zużycie dziś | INT16 | cm ×10 |
| 115 | 215 | Zużycie wczoraj | INT16 | cm ×10 |
| 116 | 216 | Średnie zużycie 7 dni | INT16 | cm/dobę ×10 |
| 117 | 217 | Średnie zużycie 30 dni | INT16 | cm/dobę ×10 |
| 118 | 218 | Prognoza „wystarczy na” | UINT16 | dni ×10, 0xFFFF = brak danych |
| 119 | 219 | Ostatnie tankowanie | INT16 | cm ×10 |
| 120–121 | 220–221 | Objętość | UINT32 | l (0xFFFFFFFF = przeliczanie wyłączone) |
| 122–123 | 222–223 | Pojemność całkowita | UINT32 | l |
| 124–125 | 224–225 | Zużycie dziś | FLOAT32 | l |
| 126–127 | 226–227 | Średnie zużycie 7 dni | FLOAT32 | l/dobę |
| 128–129 | 228–229 | rezerwa (np. temperatura) | | |

### 12.3 Wejścia dyskretne (FC02, tylko odczyt)

| Adres | SCADA | Nazwa |
|---|---|---|
| 0–7 | 10001–10008 | DI1–DI8 (1 = styk zwarty) |
| 8 | 10009 | Alarm aktywny (dowolny zbiornik) |
| 9 | 10010 | Alarm skwitowany |
| 10 | 10011 | Z1 KRYTYCZNY |
| 11 | 10012 | Z1 MINIMALNY |
| 12 | 10013 | Z1 ZA DUŻY |
| 13 | 10014 | Z1 BŁĄD CZUJNIKA |
| 14 | 10015 | Z2 KRYTYCZNY |
| 15 | 10016 | Z2 MINIMALNY |
| 16 | 10017 | Z2 ZA DUŻY |
| 17 | 10018 | Z2 BŁĄD CZUJNIKA |
| 18 | 10019 | Karta SD OK |
| 19 | 10020 | Czas zsynchronizowany |
| 20 | 10021 | Z1 NAGŁY SPADEK |
| 21 | 10022 | Z2 NAGŁY SPADEK |
| 22 | 10023 | Z1 prognoza poniżej N dni |
| 23 | 10024 | Z2 prognoza poniżej N dni |

### 12.4 Cewki (FC01 odczyt, FC05/15 zapis – gdy zapis Modbus włączony)

| Adres | SCADA | Nazwa | Opis |
|---|---|---|---|
| 0–7 | 00001–00008 | OUT1–OUT8 | odczyt: bieżący stan; zapis działa tylko dla wyjść z funkcją „ręczne” |
| 100 | 00101 | Kasuj alarm | zapis 1 = kwitowanie (jak DI1), samo wraca do 0 |
| 101 | 00102 | Test wyjść | zapis 1 = uruchom test wyjść 1–6 |

### 12.5 Rejestry holding (FC03 odczyt, FC06/16 zapis – gdy zapis Modbus włączony)

Zapis zmienia ustawienia tak samo jak strona WWW (z walidacją; błędna wartość → wyjątek Modbus 03) i zapisuje je w NVS/EEPROM.

| Adres | SCADA | Nazwa | Typ | Jedn. / zakres |
|---|---|---|---|---|
| 0–7 | 40001–40008 | Jasność PWM OUT1–8 (przed skasowaniem) | UINT16 | 0–100 % |
| 8–15 | 40009–40016 | Jasność PWM OUT1–8 (po skasowaniu) | UINT16 | 0–100 % |
| 16 | 40017 | Interwał zapisu logu | UINT16 | 1–60 min |
| 100 | 40101 | Z1 próg KRYTYCZNY | INT16 | cm ×10 |
| 101 | 40102 | Z1 próg MINIMALNY | INT16 | cm ×10 |
| 102 | 40103 | Z1 próg ZA DUŻY | INT16 | cm ×10 |
| 103 | 40104 | Z1 histereza | INT16 | cm ×10 |
| 104 | 40105 | Z1 opóźnienie alarmu | UINT16 | s |
| 105 | 40106 | Z1 wysokość zbiornika | INT16 | cm ×10 |
| 106 | 40107 | Z1 nagły spadek: próg Y | INT16 | cm ×10 |
| 107 | 40108 | Z1 nagły spadek: czas T | UINT16 | min |
| 108 | 40109 | Z1 próg tankowania | INT16 | cm ×10 |
| 109 | 40110 | Z1 ostrzeżenie prognozy | UINT16 | dni |
| 200–209 | 40201–40210 | Zbiornik 2 – jak wyżej | | |

Kalibracji MIN/MAX celowo nie da się zmienić przez Modbus (tylko przez stronę), żeby przypadkowy zapis ze SCADA nie rozkalibrował pomiaru.

Mapa rejestrów będzie też do pobrania ze strony (Ustawienia → Modbus) jako CSV dla integratora.

---

## 13. MQTT

| Parametr | Wartość |
|---|---|
| Ustawienia | wł./wył., broker (adres, port 1883), login, hasło, ID klienta, prefiks tematów (domyślnie `monitorapp/<nazwa_urządzenia>`) |
| Wysyłanie | co X s (domyślnie 10 s) oraz natychmiast przy zmianie stanu alarmu |
| Dostępność | LWT: `<prefiks>/status` = `online` / `offline` (retained) |
| Home Assistant | autodiscovery wł./wył.: encje pojawiają się same (poziom cm, %, mA, stan alarmu, przycisk „Kasuj alarm”, wyjścia) |

Tematy:

```
monitorapp/<nazwa>/status                 online / offline
monitorapp/<nazwa>/zbiornik1/poziom_cm    182.4
monitorapp/<nazwa>/zbiornik1/procent      60.8
monitorapp/<nazwa>/zbiornik1/mA           13.73
monitorapp/<nazwa>/zbiornik1/stan         NORMALNY
monitorapp/<nazwa>/zbiornik1/zuzycie_dzis 1.8
monitorapp/<nazwa>/zbiornik1/zuzycie_7d   2.1
monitorapp/<nazwa>/zbiornik1/prognoza_dni 12.5
monitorapp/<nazwa>/zbiornik1/litry       4520   (gdy przeliczanie włączone)
monitorapp/<nazwa>/zbiornik2/...          (jak wyżej)
monitorapp/<nazwa>/alarm                  {"aktywny":true,"skwitowany":false,"najgorszy":"KRYTYCZNY"}
monitorapp/<nazwa>/wyjscia                {"out1":100,"out2":40,...}   (jasność %)
monitorapp/<nazwa>/cmd/kasuj              (subskrypcja) dowolna wiadomość = kwitowanie
monitorapp/<nazwa>/cmd/out/<n>            (subskrypcja) 0–100, tylko wyjścia „ręczne”
```

Komendy przez MQTT można wyłączyć w ustawieniach (wtedy tylko wysyłanie danych).

---

## 14. Statystyki zużycia i alarm nagłego spadku (wersja 1.0)

Dostępne dla każdego zbiornika (włączane osobno), domyślnie włączone dla zbiornika paliwa. Dla zbiornika ppoż. przydaje się głównie alarm nagłego spadku (wyciek).

### 14.1 Zużycie dzienne
- Liczone od 00:00 do 23:59 czasu lokalnego jako suma spadków poziomu (na podstawie średnich minutowych z pomiarów 2 s, więc drgania nie dodają się do zużycia).
- **Tankowanie** jest rozpoznawane automatycznie: wzrost poziomu większy niż X cm (domyślnie 2 cm) w ciągu 15 min. Nie zmniejsza zużycia, trafia do dziennika zdarzeń jako „Tankowanie +N cm” i jest pokazywane osobno.
- Jednostka: litry, gdy włączone przeliczanie (sekcja 5.1), w przeciwnym razie cm i % zbiornika.
- Bieżący licznik doby zapisywany co interwał logu, więc restart w ciągu dnia go nie zeruje.

### 14.2 Średnie zużycie
- Średnia z ostatnich **7 dni** i **30 dni** (dni bez pełnych danych, np. wyłączone urządzenie, są pomijane).
- Pokazywana na pulpicie oraz na stronie Logi jako wykres słupkowy zużycia dzień po dniu, ze znacznikami tankowań.

### 14.3 Prognoza „wystarczy na X dni”
- `dni = (poziom bieżący − próg odniesienia) / średnie dzienne zużycie z 7 dni`
- Próg odniesienia do wyboru: próg MINIMALNY (domyślnie), KRYTYCZNY albo dno (0 cm).
- Wynik zaokrąglony do 0,5 dnia, z podaniem przewidywanej daty („wystarczy na ok. 12 dni, do 22.11”).
- Gdy danych jest za mało (< 3 dni) albo zużycie ≈ 0, pokazywane jest „brak danych do prognozy”.
- Opcjonalne ostrzeżenie (stan MINIMALNY w dzienniku, MQTT i Modbus), gdy prognoza spadnie poniżej N dni (np. 3 dni).

### 14.4 Alarm nagłego spadku (wyciek / kradzież)
- Warunek: spadek poziomu o więcej niż **Y cm w czasie T min** (domyślnie 3 cm w 10 min), liczony ze średnich, z progiem powyżej szumu pomiaru.
- Dodatkowo „okno nocne” (np. 22:00–6:00): każdy spadek większy niż Z cm (domyślnie 1 cm) wywołuje alarm, bo w nocy nikt nie powinien pobierać paliwa.
- Nowy stan alarmu **NAGŁY SPADEK** (kod 6 w Modbus), traktowany jak KRYTYCZNY: syrena, lampka 100%, alarm zbiorczy, MQTT, dziennik zdarzeń z wielkością i czasem spadku. Kasowanie jak w sekcji 11.3. Alarm wygasa po skasowaniu i 30 min bez dalszego spadku.
- Możliwość chwilowego wyłączenia (np. „planowany pobór przez 2 h” z przycisku na stronie), żeby zaplanowane tankowanie maszyn nie wywołało alarmu.

### 14.5 Zapis danych
- Plik roczny `/stats/2026.csv`: `data;z1_zuzycie_cm;z1_zuzycie_l;z1_tankowanie_cm;z1_tankowanie_l;z1_poziom_00;z2_...` (kolumny w litrach puste, gdy przeliczanie wyłączone), jeden wiersz na dobę (zapis o północy).
- Średnie i prognoza są liczone z tego pliku przy starcie i aktualizowane w pamięci.

### 14.6 Ustawienia (zakładka „Statystyki”)
Dla każdego zbiornika: statystyki wł./wył., próg tankowania, próg prognozy (MINIMALNY / KRYTYCZNY / dno), ostrzeżenie przy prognozie < N dni, nagły spadek wł./wył. (Y cm, T min), okno nocne wł./wył. (godziny, Z cm).

---

## 15. Pozostałe funkcje wersji 1.0

| Funkcja | Opis |
|---|---|
| **Aktualizacja OTA przez stronę** | Ustawienia → System → „Wgraj plik” (.bin programu, osobno plik strony LittleFS); pasek postępu, sprawdzenie pliku, restart; przy błędzie zostaje stara wersja |
| **Kopia ustawień** | pobranie wszystkich ustawień jako plik JSON i wgranie go z powrotem (np. na drugie urządzenie); hasła WiFi/MQTT w kopii opcjonalnie |
| **Watchdog i samodiagnostyka** | restart przy zawieszeniu, zapis przyczyny restartu w dzienniku, kontrola SD/ADS/RTC co minutę |
| **Reset przyciskiem Download** | trzymanie 10 s podczas pracy (OLED odlicza) = przywrócenie domyślnego loginu/hasła i ustawień sieci; kalibracja i progi zostają |
| **Dziennik zdarzeń** | sekcja 7 |
| **Eksport CSV** | pobieranie logów z wybranego dnia lub zakresu |

## 16. Dodatki na później

| Dodatek | Opis |
|---|---|
| Automatyczne uzupełnianie zbiornika ppoż. | pompa/elektrozawór: włącz poniżej MINIMALNEGO, wyłącz przy zadanym poziomie, maks. czas pracy jako zabezpieczenie (12/24 V DC ≤ 2 A, większe przez przekaźnik/stycznik) |
| Pływaki na wejściach DI2–DI8 | niezależne czujniki „suchobieg” i „przelew”, redundancja ważna dla instalacji ppoż. |
| Powiadomienia | Telegram, e-mail SMTP przy alarmie i powrocie do normy |
| Modbus RTU (RS485) | ta sama mapa rejestrów po RS485 |
| Temperatura DS18B20 | temperatura paliwa/otoczenia, alarm przymrozku |
| Konto „tylko podgląd” | drugi użytkownik bez dostępu do ustawień |

---

## 17. Bezpieczeństwo i niezawodność

- Hasło jako skrót z solą, blokada po błędnych próbach, wymuszona zmiana domyślnego hasła.
- HTTP bez szyfrowania (HTTPS na ESP32 jest możliwy, ale wolny i z certyfikatem, którego przeglądarka nie zaufa). Modbus TCP i MQTT też bez szyfrowania. Urządzenie powinno pracować w sieci lokalnej, nie wystawione do internetu.
- Modbus domyślnie tylko do odczytu, opcjonalna lista dozwolonych IP.
- API tylko z sesją lub tokenem (skrót SHA-256 w pamięci, uprawnienia, odwoływanie, limit zapytań).
- Watchdog, wykrycie braku karty SD / ADS / RTC bez zatrzymania pracy.
- Brak czasu (RTC z flagą OSF i brak NTP): ostrzeżenie na stronie i OLED, logi zapisywane mimo to.
- Wyjścia po restarcie zawsze startują wyłączone, a stan alarmu jest liczony od nowa z pomiaru.

---

## 18. Etapy realizacji

Każdy etap kończy się wgraniem na płytkę (`upload.ps1`) i sprawdzeniem na żywo.

| Etap | Zakres |
|---|---|
| 1 | Szkielet: moduły, partycje OTA, NVS + EEPROM, pomiar z uśrednianiem (A3/A4), OLED z logo 5 s, test wyjść 1–6, Ethernet + AP, logowanie, pulpit |
| 2 | Ustawienia sieci: WiFi (skan, łączenie, DHCP/statyczny), Ethernet statyczny, AP, hasło, tokeny API |
| 3 | Przetworniki: kalibracja MIN/MAX, przeliczanie na litry (walec leżący i inne), progi alarmowe w cm, stany na pulpicie i OLED |
| 4 | Wyjścia: przypisanie funkcji, PWM, logika alarmu, kasowanie DI1 i przez stronę; **test lampki z buzzerem** |
| 5 | Czas: DS3231, NTP, ustawianie z przeglądarki, strefa czasowa |
| 6 | Logi SD: pliki dzienne, dziennik zdarzeń, strona Logi z wykresem, pobieranie CSV |
| 7 | Statystyki: zużycie dzienne i średnie, tankowania, prognoza, alarm nagłego spadku, wykres słupkowy |
| 8 | Modbus TCP wg mapy z sekcji 12, MQTT + Home Assistant |
| 9 | OTA, kopia ustawień, watchdog, reset przyciskiem, testy całości |

Struktura folderu `02_Monitor_App/`: szkic `.ino` + moduły `.h/.cpp`, folder `data/` z plikami strony (wgrywane do LittleFS), `partitions.csv`, `README.md`, `MODBUS.md` (mapa rejestrów dla integratora), `API.md` (dokumentacja API).

Do pierwszej kalibracji potrzebne będą: podłączony przetwornik oraz pomiar przy dwóch znanych poziomach (np. pusty i zmierzony miarką).
