# MonitorUp – Modbus TCP

| Parametr | Wartość |
|---|---|
| Port | 502 (Ustawienia → Modbus; Modbus domyślnie wyłączony) |
| Unit ID | 1 (odpowiada też na 0 i 255) |
| Klienci | do 4 jednocześnie, Ethernet i WiFi |
| Funkcje | 01, 02, 03, 04, 05, 06, 15, 16 |
| Adresy | od 0 (0 = 30001 / 40001 w notacji SCADA) |
| 32-bit | UINT32 / FLOAT32 w dwóch rejestrach, starsze słowo pierwsze (ABCD) |
| Brak pomiaru | INT16 = -32768 (0x8000), FLOAT32 = NaN |
| Zapis | domyślnie wyłączony (wyjątek 01); błędna wartość = wyjątek 03; opcjonalna lista dozwolonych IP |

Pełna mapa (plik CSV do zaimportowania) jest do pobrania ze strony: Ustawienia → Modbus → „Pobierz mapę rejestrów”, oraz w `web_app.h` (`MODBUS_CSV`). Skrót:

## Rejestry wejściowe (FC04)

| Adres | Nazwa | Typ / jednostka |
|---|---|---|
| 0 | wersja programu | 46 = 0.4.6 |
| 1 | status (b0 SD, b1 ADS, b2 RTC, b3 czas, b4 ETH, b5 WiFi, b6 MQTT, b7 alarm, b8 skwitowany, b9 test) | bity |
| 2–3 | czas pracy | UINT32 s |
| 4–9 | rok, miesiąc, dzień, godzina, minuta, sekunda | czas lokalny |
| 10 | najgorszy stan | kod stanu |
| 11 / 12 | wyjścia OUT1–8 / wejścia DI1–8 | bity |
| 13 | wolne miejsce SD | MB |
| 14 | temperatura DS3231 | °C ×10 |
| **100–129** | **zbiornik 1 (paliwo, A4 – domyślnie włączony)** | |
| **200–229** | **zbiornik 2 (A3 – domyślnie wyłączony)** – ten sam układ | |
| +0 / +1 / +2 | poziom cm ×10 / % ×10 / mA ×100 | INT16 |
| +3 | stan: 0 NORMALNY, 1 MINIMALNY, 2 KRYTYCZNY, 3 ZA DUŻY, 4 BŁĄD CZUJNIKA, 5 WYŁĄCZONY, 6 NAGŁY SPADEK | UINT16 |
| +4 | flagi: b0 skwitowany, b1 pomiar ważny, b2 kalibracja | bity |
| +5 | trend cm/h ×10 | INT16 |
| +6 / +7 | surowy ADC / napięcie mV | INT16 |
| +8…+13 | poziom cm, %, mA | FLOAT32 |
| +14 / +15 | zużycie dziś / wczoraj, cm ×10 | INT16 |
| +16 / +17 | średnie zużycie 7 / 30 dni, cm/dobę ×10 | INT16 |
| +18 | prognoza „wystarczy na”, dni ×10 (65535 = brak) | UINT16 |
| +19 | ostatnie tankowanie, cm ×10 | INT16 |
| +20…+23 | objętość / pojemność, l | UINT32 (0xFFFFFFFF = bez litrów) |
| +24…+27 | zużycie dziś / średnie 7 dni, l | FLOAT32 |

## Wejścia dyskretne (FC02)

0–7 DI1–8 · 8 alarm aktywny · 9 skwitowany · 10–13 Z1 KRYT/MIN/ZA DUŻY/BŁĄD · 14–17 Z2 · 18 SD OK · 19 czas OK · 20–21 nagły spadek Z1/Z2 · 22–23 prognoza < N dni Z1/Z2

## Cewki (FC01 / 05 / 15)

0–7 OUT1–8 (zapis tylko w trybie „ręczne”) · 100 kasuj alarm · 101 test wyjść

## Rejestry holding (FC03 / 06 / 16)

0–7 jasność OUT1–8 przed skasowaniem (%) · 8–15 po skasowaniu (%) · 16 interwał logu (min) ·
100/200 próg KRYTYCZNY · 101/201 MINIMALNY · 102/202 ZA DUŻY · 103/203 histereza (cm ×10) · 104/204 opóźnienie (s) · 105/205 wysokość (cm ×10) ·
106/206 nagły spadek Y (cm ×10) · 107/207 czas T (min) · 108/208 próg tankowania (cm ×10) · 109/209 ostrzeżenie prognozy (dni)

Kalibracji nie można zmienić przez Modbus.
