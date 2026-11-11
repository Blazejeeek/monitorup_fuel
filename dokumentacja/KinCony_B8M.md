# KinCony B8M – sterownik ESP32-S3 z 8 wyjściami MOSFET

Dokumentacja techniczna przygotowana na potrzeby projektu **Monitoring poziomu paliwa / wody**.
Stan wiedzy: 10.11.2026. Źródła są wymienione na końcu dokumentu, a przy każdej sekcji podano, skąd pochodzą dane.

**Legenda wiarygodności**

| Oznaczenie | Znaczenie |
|---|---|
| ✅ | Dane producenta (strona produktu, sklep KinCony, wątek admina na forum KinCony) |
| ⚠️ | Dane niepewne: z recenzji, od resellera, z odpowiedzi dotyczącej innego modelu albo wywnioskowane |
| ❓ | Brak danych w dostępnych źródłach, trzeba zweryfikować na płytce lub u producenta |

---

## 1. Podsumowanie

| Parametr | Wartość | Źr. |
|---|---|---|
| Model | KinCony B8M (8CH MOSFET) | ✅ |
| Mikrokontroler | ESP32-S3-WROOM-1U (N16R8: 16 MB Flash, 8 MB PSRAM), złącze na antenę zewnętrzną (wersja „U”) | ✅ |
| Zasilanie | 12–24 V DC | ✅ |
| Wyjścia | 8 × MOSFET, DC, „MAX 10A driver IC” na kanał | ✅ |
| Wejścia cyfrowe | 8 × suchy styk (dry contact), izolacja optoelektroniczna, kabel do 500 m | ✅ |
| Wejścia analogowe | 4 × ADS1115 16-bit: 2 × 0–5 V DC (A1, A2), 2 × 4–20 mA (A3, A4) | ✅ |
| 1-Wire | 4 × GPIO z rezystorem podciągającym na PCB | ✅ |
| Wolne GPIO | 3 × GPIO bez rezystora podciągającego, bezpośrednio z ESP32 | ✅ |
| Komunikacja | Ethernet RJ45 100 Mbps (W5500), Wi-Fi, Bluetooth, RS485, USB-C, I2C (złącze rozszerzeń), moduł Tuya | ✅ |
| Peryferia na płytce | RTC DS3231 (gniazdo baterii), wyświetlacz OLED SSD1306, slot karty SD (SPI), EEPROM 24C02 | ✅ |
| Przyciski | ESP32 Reset, ESP32 Download, Tuya config | ✅ |
| Wymiary | 122 × 83 × 59 mm | ✅ |
| Montaż | szyna DIN (35 mm) | ✅ |
| Oprogramowanie | KCS v3 (MQTT + autodiscovery Home Assistant), ESPHome, Tasmota, Arduino IDE, MicroPython, ESP-IDF, Apple HomeKit, chmura KinCony / Tuya | ✅ |

---

## 2. Pinout ESP32-S3

Źródło: oficjalny wątek „B8M ESP32-S3 IO pins define” na forum KinCony (admin, ostatnia edycja 16.01.2026) oraz oficjalny YAML ESPHome. ✅

### 2.1 Magistrala I2C

| Sygnał | GPIO |
|---|---|
| SDA | **GPIO8** |
| SCL | **GPIO18** |

W konfiguracji ESPHome producenta magistrala pracuje z częstotliwością 400 kHz.

### 2.2 Urządzenia na magistrali I2C

| Układ | Adres | Funkcja |
|---|---|---|
| PCF8575 (ekspander 16-bit) | **0x22** | P0–P7: wyjścia MOSFET 1–8, P8–P15: wejścia cyfrowe DI1–DI8 |
| ADS1115 (ADC 16-bit, 4 kanały) | **0x48** | wejścia analogowe A1–A4 |
| DS3231 (RTC) | **0x68** | zegar czasu rzeczywistego |
| SSD1306 (OLED 128×64) | **0x3C** | wyświetlacz |
| 24C02 (EEPROM) | **0x50** | pamięć konfiguracji |

> ⚠️ Recenzja haade.fr wspomina, że adresy ekspandera mogą się różnić między rewizjami płytki (pada też adres 0x24). Przy pierwszym uruchomieniu warto włączyć `scan: true` w ESPHome i sprawdzić adresy w logu.

### 2.3 Wyjścia MOSFET (przez ekspander PCF8575 @0x22)

| Wyjście | Pin PCF8575 | Logika |
|---|---|---|
| OUT1 | P0 | odwrócona (`inverted: true`) |
| OUT2 | P1 | odwrócona |
| OUT3 | P2 | odwrócona |
| OUT4 | P3 | odwrócona |
| OUT5 | P4 | odwrócona |
| OUT6 | P5 | odwrócona |
| OUT7 | P6 | odwrócona |
| OUT8 | P7 | odwrócona |

Wyjścia **nie są** podłączone bezpośrednio do GPIO ESP32. Sterowanie odbywa się przez I2C, więc czas przełączenia zależy od magistrali (pojedyncze ms). Nie nadaje się to do PWM wysokiej częstotliwości (np. ściemniania LED).

### 2.4 Wejścia cyfrowe (przez ekspander PCF8575 @0x22)

| Wejście | Pin PCF8575 | Logika |
|---|---|---|
| DI1–DI8 | P8–P15 | odwrócona (`inverted: true`): zwarcie styku daje stan aktywny |

### 2.5 Wejścia analogowe (ADS1115 @0x48)

| Zacisk | Kanał ADS1115 | Zakres |
|---|---|---|
| A1 | A0-GND | 0–5 V DC |
| A2 | A1-GND | 0–5 V DC |
| A3 | A2-GND | 4–20 mA |
| A4 | A3-GND | 4–20 mA |

> ⚠️ Przyporządkowanie zacisków A1–A4 do kanałów ADS1115 A0–A3 wywnioskowano z kolejności w YAML producenta. Producent nie podaje go wprost.

### 2.6 GPIO bezpośrednio z ESP32

| GPIO | Przeznaczenie | Rezystor podciągający na PCB |
|---|---|---|
| GPIO47 | 1-Wire / GPIO | tak |
| GPIO48 | 1-Wire / GPIO | tak |
| GPIO7 | 1-Wire / GPIO | tak |
| GPIO40 | 1-Wire / GPIO | tak |
| GPIO13 | wolne GPIO | nie |
| GPIO14 | wolne GPIO | nie |
| GPIO21 | wolne GPIO | nie |
| GPIO0 | przycisk Download (pin „strapping”) | ⚠️ w YAML opisany jako „bez rezystora”, ale jest to pin bootowania, nie używać do własnych sygnałów |

Wolne GPIO pracują na poziomie **3,3 V** i nie mają zabezpieczeń. Nie wolno podawać na nie 5 V ani 12/24 V.

### 2.7 Ethernet (W5500, SPI)

| Sygnał | GPIO |
|---|---|
| CLK | GPIO1 |
| MOSI | GPIO2 |
| MISO | GPIO41 |
| CS | GPIO42 |
| INT | GPIO43 |
| RESET | GPIO44 |

### 2.8 RS485

| Sygnał | GPIO |
|---|---|
| RX | GPIO38 |
| TX | GPIO39 |

Przykład producenta używa 9600 bps. Kierunek transmisji przełączany jest sprzętowo (YAML nie definiuje pinu DE/RE). ⚠️ Izolację portu RS485 deklaruje tylko reseller, producent o niej nie pisze.

### 2.9 Karta SD (SPI)

| Sygnał | GPIO |
|---|---|
| MOSI | GPIO10 |
| SCK | GPIO11 |
| MISO | GPIO12 |
| CS | GPIO9 |
| CD (wykrycie karty) | GPIO5 |

### 2.10 Moduł Tuya (UART)

| Sygnał | GPIO ESP32 |
|---|---|
| RX (ESP32) | GPIO17 |
| TX (ESP32) | GPIO16 |

Przycisk parowania jest na pinie **P28 modułu Tuya**, a dioda sieci na pinie **P16 modułu Tuya**. To piny modułu Tuya, nie ESP32, więc nie ma konfliktu z GPIO16. UART Tuya pracuje z prędkością 9600 bps.

### 2.11 Zestawienie zajętych GPIO

| GPIO | Funkcja |
|---|---|
| 0 | przycisk Download (strapping) |
| 1, 2, 41, 42, 43, 44 | Ethernet W5500 |
| 5, 9, 10, 11, 12 | karta SD |
| 7, 40, 47, 48 | 1-Wire (wyprowadzone na zaciski) |
| 8, 18 | I2C |
| 13, 14, 21 | wolne GPIO (wyprowadzone na zaciski) |
| 16, 17 | UART do modułu Tuya |
| 38, 39 | RS485 |

---

## 3. Zasilanie

| Parametr | Wartość | Źr. |
|---|---|---|
| Napięcie zasilania | 12–24 V DC | ✅ |
| Napięcie obciążeń na wyjściach MOSFET | takie samo jak zasilanie płytki. Nie da się mieszać 12 V i 24 V na różnych kanałach („only work with one voltage, 12v or 24v”) | ✅ admin forum |
| Pobór prądu własny | ❓ nie podano | ❓ |
| Zabezpieczenia | ⚠️ reseller wymienia zabezpieczenie przed odwrotną polaryzacją, diody TVS i diody gaszące na wyjściach. Producent tego nie potwierdza | ⚠️ |
| USB-C | programowanie. Recenzja haade.fr podaje, że zasila też płytkę; reseller pisze, że tylko flashowanie. Przy USB nie działają wyjścia MOSFET (brak 12/24 V) | ⚠️ |
| Zestaw „Bundle B” | zasilacz 12 V 2 A + kabel USB-C (dotyczy B4M w odpowiedzi admina; dla B8M sklep podaje „B8M + USB-C cable + power supply”) | ✅ / ⚠️ |

---

## 4. Wyjścia MOSFET: parametry i maksymalne obciążenie

| Parametr | Wartość | Źr. |
|---|---|---|
| Liczba kanałów | 8 | ✅ |
| Rodzaj | tranzystor MOSFET, **tylko DC**, nie przełącza 230 V AC | ✅ |
| Napięcie obciążenia | 12–24 V DC (= napięcie zasilania płytki) | ✅ |
| Prąd na kanał | **„MAX 10A driver IC”**, czyli maks. 10 A wg producenta | ✅ |
| Zalecany prąd ciągły na kanał | ⚠️ admin forum w wątku o bliźniaczym B4M: „<2A is ok” | ⚠️ |
| Prąd łączny (wszystkie kanały) | ❓ **producent nie podaje** | ❓ |
| Typ klucza (high-side / low-side) | ❓ nie podano tekstowo. Schemat podłączenia jest tylko w obrazku na forum (wątek tid=9251) | ❓ |
| Układ sterownika / tranzystora | ❓ nie podano oznaczenia | ❓ |
| Częstotliwość przełączania | niska (sterowanie przez I2C), brak sprzętowego PWM | ✅ wynik z architektury |
| Dioda gasząca dla obciążeń indukcyjnych | ⚠️ reseller twierdzi, że jest na płytce. Bezpieczniej dodać własną diodę przy elektrozaworach i pompach | ⚠️ |

**Jak to interpretować w praktyce**

- Liczbę „10 A” trzeba traktować jako parametr samego układu sterownika lub tranzystora, a nie gwarantowaną obciążalność całej płytki. O realnym limicie decydują ścieżki PCB, zaciski śrubowe, chłodzenie i zasilacz.
- Do projektowania przyjmujemy bezpiecznie **≤ 2 A ciągłego prądu na kanał**, zgodnie z odpowiedzią admina dla B4M, oraz **łączny prąd ograniczony zasilaczem i bezpiecznikiem**, np. ≤ 8–10 A sumarycznie. Szacunek łącznego prądu jest **nieoficjalny**: wynika z typowych zacisków i ścieżek, nie z dokumentacji. Przed obciążeniem większym prądem warto zapytać producenta na forum.
- Obciążenia powyżej tych wartości albo zasilane z innego napięcia, np. pompa 230 V, należy podłączać przez zewnętrzny przekaźnik lub stycznik sterowany wyjściem MOSFET.
- Na linii zasilania obciążeń warto dać bezpiecznik dobrany do sumarycznego prądu.

---

## 5. Wejścia: parametry

### 5.1 Wejścia cyfrowe DI1–DI8

| Parametr | Wartość | Źr. |
|---|---|---|
| Liczba | 8 | ✅ |
| Typ | suchy styk (bezpotencjałowy), izolacja transoptorowa | ✅ |
| Długość kabla | do 500 m | ✅ |
| Logika | aktywne przy zwarciu styku (w ESPHome `inverted: true`) | ✅ |
| Napięcie / prąd pętli | ❓ nie podano. Napięcie wejściowe pochodzi z płytki (suchy styk), **nie podawać zewnętrznego napięcia** | ❓ |
| Zgodność z czujnikami NPN/PNP | ❓ nie podano. Czujnik typu NPN z otwartym kolektorem zwykle działa jak suchy styk, ale trzeba to sprawdzić | ❓ |

Zastosowanie w projekcie: pływakowe czujniki poziomu (min/max), krańcówki, sygnał „przelew”.

### 5.2 Wejścia analogowe A1–A4 (ADS1115)

| Parametr | Wartość | Źr. |
|---|---|---|
| Przetwornik | ADS1115, 16 bit, I2C @0x48 | ✅ |
| A1, A2 | 0–5 V DC | ✅ |
| A3, A4 | 4–20 mA DC | ✅ |
| Wzmocnienie w przykładzie producenta | `gain: 6.144` (zakres ±6,144 V, rozdzielczość ok. 0,1875 mV/LSB) | ✅ |
| Rezystor bocznikowy dla 4–20 mA | ❓ wartość nie podana, a YAML producenta odczytuje surowe napięcie bez przeliczania | ❓ |
| Zasilanie pętli 4–20 mA | ⚠️ reseller pisze „loop-powered”. Nie wiadomo, czy płytka zasila pętlę. Najczęściej czujnik zasila się z 12/24 V płytki, a sygnał wraca na A3/A4 | ⚠️ |
| Ochrona wejść / izolacja | ❓ nie podano | ❓ |

**Przeliczanie dla czujników poziomu**

Czujnik 0–5 V (A1/A2), np. sonda hydrostatyczna 0–5 m:

```
poziom [m] = U [V] / 5 V × zakres_czujnika [m]
```

Czujnik 4–20 mA (A3/A4): odczyt jest napięciem na nieznanym boczniku, więc trzeba go skalibrować dwupunktowo:

1. Zmierz napięcie przy pustym zbiorniku (4 mA) → `U_min`.
2. Zmierz napięcie przy pełnym zakresie (20 mA) lub przy znanym poziomie → `U_max`.
3. `poziom = (U − U_min) / (U_max − U_min) × zakres_czujnika`

Przykład w ESPHome (wartości kalibracyjne do uzupełnienia):

```yaml
sensor:
  - platform: ads1115
    multiplexer: 'A2_GND'      # zacisk A3 (4-20 mA), patrz uwaga w 2.5
    gain: 6.144
    name: "Poziom zbiornika"
    update_interval: 5s
    unit_of_measurement: "cm"
    filters:
      - calibrate_linear:
          - 0.48 -> 0       # U przy 4 mA (przykład dla bocznika 120 Ω, do pomiaru)
          - 2.40 -> 200     # U przy 20 mA = pełny zakres czujnika
      - sliding_window_moving_average:
          window_size: 6
          send_every: 1
```

> ⚠️ Wartości 0,48 V i 2,40 V są tylko przykładem dla bocznika 120 Ω. Prawdziwe wartości trzeba zmierzyć na własnej płytce.

### 5.3 1-Wire i wolne GPIO

| Parametr | Wartość |
|---|---|
| 1-Wire (GPIO47, 48, 7, 40) | podciągnięte na PCB, gotowe np. dla DS18B20 (temperatura cieczy lub otoczenia) |
| Wolne GPIO (13, 14, 21) | 3,3 V, bez zabezpieczeń i bez podciągania, np. dla ultradźwiękowego czujnika JSN-SR04T (TRIG/ECHO). ⚠️ Sygnał ECHO 5 V wymaga dzielnika lub konwertera poziomów |

---

## 6. Interfejsy komunikacyjne

| Interfejs | Szczegóły | Zastosowanie w projekcie |
|---|---|---|
| Ethernet | W5500, 100 Mbps, IPv4/IPv6 | stabilne połączenie z Home Assistant / MQTT |
| Wi-Fi / BT | ESP32-S3, 2,4 GHz, antena zewnętrzna | ⚠️ wg haade.fr w ESPHome Wi-Fi i Ethernet nie działają jednocześnie |
| RS485 | GPIO38/39 | czujniki poziomu Modbus RTU (np. sondy radarowe lub hydrostatyczne z wyjściem RS485) |
| I2C (złącze) | GPIO8/18, wspólne z układami na płytce | dodatkowe czujniki, uwaga na konflikty adresów z tabelą 2.2 |
| USB-C | programowanie (przytrzymać „Download” przy włączaniu przy pierwszym flashowaniu) | |
| Moduł Tuya | UART GPIO16/17 | integracja z aplikacją Tuya / Smart Life |

---

## 7. Uwagi dla projektu monitoringu poziomu paliwa / wody

1. **Pomiar ciągły poziomu:** najlepiej sondy 4–20 mA na A3/A4, odporne na zakłócenia na długich kablach. Do paliwa trzeba wybrać sondę z certyfikatem do olejów i paliw (ewentualnie ATEX, zależnie od instalacji).
2. **Progi alarmowe:** pływaki jako suchy styk na DI1–DI8, niezależne od pomiaru analogowego, co daje redundancję.
3. **Wyjścia:** sterowanie pompą lub elektrozaworem 12/24 V DC bezpośrednio z MOSFET (≤ 2 A zalecane). Pompy 230 V tylko przez przekaźnik lub stycznik.
4. **Rejestracja:** karta SD i RTC DS3231 pozwalają zapisywać dane lokalnie przy braku sieci.
5. **Temperatura:** DS18B20 na 1-Wire, przydatna do kompensacji temperaturowej gęstości paliwa.

---

## 8. Otwarte pytania (do weryfikacji)

- Typ klucza wyjściowego (high-side czy low-side), czyli jak podłączyć obciążenie. Odpowiedź jest na obrazku `B8M_wiring.jpg` w wątku forum tid=9251.
- Maksymalny prąd łączny wszystkich wyjść i prąd ciągły na kanał (10 A u producenta, „<2 A ok” u admina dla B4M).
- Wartość bocznika na wejściach 4–20 mA i to, czy płytka zasila pętlę.
- Pobór prądu własnego płytki.
- Zabezpieczenia (odwrotna polaryzacja, TVS, diody gaszące) potwierdzone tylko przez resellera.

---

## 9. Źródła

| Źródło | Typ | Wiarygodność |
|---|---|---|
| [Strona produktu KinCony B8M](https://www.kincony.com/esp32-smart-controller-b8m-8ch-mosfet.html) | producent | ✅ |
| [Sklep KinCony – B8M](https://shop.kincony.com/products/esp32-smart-controller-b8m-8ch-mosfet) | producent | ✅ |
| [KinCony B8M ESP32-S3 Smart Controller released](https://www.kincony.com/kincony-b8m-esp32-s3-smart-controller-released.html) | producent | ✅ |
| [Forum: B8M ESP32-S3 IO pins define (tid=8209)](https://www.kincony.com/forum/showthread.php?tid=8209) | producent (admin) | ✅ |
| [Forum: B8M ESPHome YAML (tid=8210)](https://www.kincony.com/forum/showthread.php?tid=8210) | producent (admin) | ✅ |
| [Forum: B8M Smart Controller Wiring Examples (tid=9251)](https://www.kincony.com/forum/printthread.php?tid=9251) | producent (schemat tylko jako obrazek, nieodczytany) | ✅ |
| [Forum: Questions on the B4M (tid=9633)](https://www.kincony.com/forum/showthread.php?tid=9633) | admin, model bliźniaczy | ⚠️ |
| [ESPHome Devices – KinCony-B8M](https://devices.esphome.io/devices/kincony-b8m/) | społeczność | ✅/⚠️ |
| [Haade.fr – test B8M do nawadniania](https://haade.fr/en/blog/test-kincony-b8m-esp32-mosfet-8ch-controller-esphome-irrigation) | recenzja | ⚠️ |
| [Feedcurrent – B8M](https://www.feedcurrent.com/products/8-channel-mosfet-smart-controller-kincony-b8m) | reseller (miejscami sprzeczny z producentem, np. podaje 3 × 1-Wire i 4 × GPIO) | ⚠️ |
| [Instrukcja firmware KCS v3](https://www.kincony.com/how-to-use-kcsv3-firmware-esp32-board.html) | producent | ✅ |
