# MonitorUp.pl – jak testować sterownik

Instrukcja testu funkcji alarmowych sterownika (KinCony B8M, HW 1.0) zadajnikiem 4–20 mA i przez API.
Dotyczy oprogramowania **0.5.2** i nowszych (od 0.5.2 buzer gra w rytmie czerwonej kontrolki). Wyniki ostatniego testu są na końcu pliku.

## 1. Stanowisko

| Element | Opis |
|---|---|
| Sterownik | KinCony B8M, wejście A4 (4–20 mA, rezystor 150 Ω) |
| Zadajnik | FNIRSI SG-004A w trybie **XMT (pasywny)**, sterowany z Raspberry Pi „masim” (Modbus przez USB) |
| Raspberry Pi | `ssh masim` (LAN biura) albo `ssh masim-vpn` (WireGuard); łączy się ze sterownikiem przez jego Access Point `MonitorUp.pl-XXXX` (192.168.4.1) |
| Skrypty | katalog `stanowisko/` w repozytorium; na Pi w katalogu domowym (`scp stanowisko/*.py stanowisko/*.sh masim:`) |

Płytka **nie może być podłączona do Pi przez USB** (pętla masy przekłamuje pomiar) – oprogramowanie wgrywa się przez API, **tylko plik podpisany** (od 0.5.4):
`scp 02_Monitor_App/build/MonitorUp_vX.Y.Z.bin masim:fw/MonitorUp-X.Y.Z.ino.bin` i `ssh masim 'fw-flash ota ~/fw/MonitorUp-X.Y.Z.ino.bin'`.

## 2. Przeliczenie cm ↔ mA

Poziom = (prąd − MIN mA) × (MAX cm − MIN cm) / (MAX mA − MIN mA) × 1000 / gęstość + przesunięcie sondy.
Dla ustawień testowych (4 mA = 0 cm, 20 mA = 200 cm, gęstość 1000, przesunięcie 0): **cm = (mA − 4) × 12,5**, czyli **mA = 4 + cm / 12,5**.

Progi bieżące odczytasz w Ustawieniach → Alarmy (podgląd w cm) albo `GET /api/v1/config` (`thrCrit`, `thrMin`, `thrHigh`).
Na płytce testowej: KRYTYCZNY ≤ 30 cm (6,4 mA), MINIMALNY ≤ 60 cm (8,8 mA), ZA DUŻY ≥ 189,9 cm (19,19 mA), histereza 4 cm (0,32 mA), opóźnienie alarmu 10 s.

## 3. Narzędzia (na Pi)

| Polecenie | Co robi |
|---|---|
| `python3 sym.py <mA> [s]` | ustawia prąd zadajnikiem, czeka `s` sekund (domyślnie 15), pokazuje stan sterownika z API, wyjścia i nowe zdarzenia |
| `python3 sym.py ack [s]` | wycisza buzer i kasuje alarm przez API (jak przycisk IN1) |
| `python3 blink.py [s]` | mierzy rytm czerwonej kontrolki (WY3) i buzera (WY4) z pola `out` statusu – faktyczny stan wyjść, próbki co ok. 40 ms |
| `python3 fault.py [max_s]` | test ręczny usterki czujnika: czeka na odłączenie A4, mierzy wszystkie wyjścia (WY1–WY5), czeka na ponowne podłączenie |
| `python3 caltest.py` | kalibracja +10 cm, po 3 min −10 cm, obserwacja 4 min: nie może być TANKOWANIA ani NAGŁEGO SPADKU; na koniec przesunięcie jak przed testem |
| `python3 modbus_test.py [--zapis] [--dynamika]` | test Modbus TCP: wszystkie odczyty porównane z API, wyjątki, Unit ID, 4 klientów + odrzucenie 5., zapis zablokowany; `--zapis` FC05/06/16 (włącza zapis na czas testu, przywraca); `--dynamika` stany z zadajnika i kasowanie alarmu cewką 100. Z innego komputera: `--host IP --bez-api` |
| `bash buzzer_test.sh` | po podłączeniu A4: krytyczny, przepełnienie i nagły spadek z pomiarem kontrolki i buzera (ok. 25 min) |
| `python3 dropen.py 0\|1` | wyłącza / włącza alarm nagłego spadku (wyłączenie gasi zatrzaśnięty alarm) |
| `bash droptest.sh` | pełny test nagłego spadku (ok. 20 min): 14 mA → czeka na koniec „tankowania” → 12,8 mA → pomiar migania → wyciszenie |
| `masim plytka` | krótki stan sterownika |

Odczyt wyników z API (`GET /api/v1/status`):
- `tanks[0].state`: 0 NORMALNY (POPRAWNY), 1 MINIMALNY, 2 KRYTYCZNY, 3 ZA DUŻY, 4 BŁĄD CZUJNIKA, 6 NAGŁY SPADEK;
- `out` – bity wyjść: 1 = WY1 zielona, 2 = WY2 pomarańczowa, 4 = WY3 czerwona, 8 = WY4 buzer, 16 = WY5 przekaźnik usterki (zasilony = brak usterki).
  Przykład: `17` = zielona + WY5 (wszystko w porządku), `28` = czerwona + buzer + WY5. Przy miganiu `out` zmienia się między odczytami;
- `alarm.active`, `alarm.acked`, `alarm.warn`; dziennik: `GET /api/v1/events`.

## 4. Testy

Kolejność ma znaczenie (patrz pułapki): najpierw 1–4 i 8 z **wyłączonym** alarmem nagłego spadku (`dropen.py 0`), potem włączony alarm spadku i test 5.

| Nr | Test | Polecenie | Oczekiwany wynik |
|---|---|---|---|
| 1 | Poziom poprawny | `sym.py 14 25` | stan 0, `out` 17: zielona, WY5 zasilony, buzer wyłączony |
| 2 | Poziom minimalny | `sym.py 7.6 25` | stan 1, `out` 18: pomarańczowa, `alarm.warn` = true, bez buzera; zmiana stanu po 10 s (opóźnienie) |
| 3 | Poziom krytyczny | `sym.py 5.2 25` + `blink.py` | stan 2, `out` 28: czerwona **świeci ciągle**, buzer **gra ciągle** |
| 4 | Przepełnienie (ZA DUŻY) | `sym.py 19.8 25` + `blink.py` | stan 3: czerwona i buzer **1× na sekundę**, razem |
| 5 | Nagły spadek | `dropen.py 1`, potem `droptest.sh` | stan 6: czerwona i buzer **2× na sekundę**; zdarzenie „NAGŁY SPADEK o … cm” |
| 6 | Usterka czujnika – przerwa w pętli | `fault.py` + **ręcznie**: odłącz przewód od A4 na ≥ 10 s | stan 4: czerwona i buzer **3× na sekundę**, zielona i pomarańczowa zgaszone, WY5 puszcza (`out` 0/12 na zmianę) |
| 7 | Usterka czujnika – za duży prąd | **ręcznie**: zadajnik z wyjściem czynnym > 21 mA | jak w teście 6 |
| 8 | Wyciszenie | `sym.py ack 3` (albo przycisk IN1) | bit 8 (buzer) znika z `out`, `alarm.acked` = true, kontrolka nadal świeci / miga |
| – | Koniec | `sym.py 14 25`, `sym.py ack 3`, `dropen.py 1` | stan 0, alarm spadku włączony |

Dodatkowo, ręcznie przy płytce:
- przycisk **IN1** wycisza buzer (jak test 8);
- styki **WY5** – przy usterce i po wyłączeniu zasilania przekaźnik puszcza (styki zwarte);
- **OLED** – stan i poziom zgodne ze stroną;
- **kalibracja** – Ustawienia → Przetworniki → aktualny poziom [cm] → „Kalibruj”; odczyt ma być równy wpisanej wartości (przez API: `POST /api/v1/calibrate {"tank":0,"point":"level","cm":…}`; po teście przywróć przesunięcie).

## 5. Pułapki

| Objaw | Przyczyna | Co zrobić |
|---|---|---|
| Testy 3/4 pokazują NAGŁY SPADEK | Alarm spadku jest **zatrzaskiwany** – trwa do skasowania i 30 min bez dalszego spadku, ma pierwszeństwo przed innymi stanami | testy 1–4 przy `dropen.py 0`; na koniec `dropen.py 1` |
| Spadek poziomu nie włącza NAGŁEGO SPADKU | Po każdym wzroście poziomu sterownik uznaje **tankowanie** (`stats.refilling` = true) i wstrzymuje alarm spadku do 15 min bez wzrostu; spadek jest sprawdzany co minutę | po wzroście czekać na `refilling` = false (robi to `droptest.sh`) |
| Zadane 2 mA daje 3,9 mA, zadane 22 mA daje 20,06 mA | Zadajnik w trybie **XMT (pasywnym)** daje tylko 4,05–20,05 mA | usterkę czujnika (< 3,6 / > 21 mA) sprawdzać ręcznie – testy 6 i 7 |
| Chwilowy „BŁĄD CZUJNIKA” przy zmianie prądu | Połączenie Pi z zadajnikiem chwilowo przerywa pętlę (ok. 2 s), potem stany pośrednie | po zmianie czekać ≥ 25 s; zdarzenia z tej chwili pomijać |
| Buzer wyciszony zaraz po starcie (SW ≤ 0.5.1) | Stale aktywne wejście IN1 (`di` = 1) – stary program po starcie brał je za naciśnięcie | od 0.5.2 poprawione; stale aktywne IN1 blokuje jednak przycisk wyciszenia – sprawdzić okablowanie IN1 |
| Po skasowaniu zatrzaśnięcia (`dropen.py 0/1`) od razu nowy NAGŁY SPADEK | spadek nadal jest w historii z ostatnich 10 min | `dropen.py 0`, odczekać 11 min, `dropen.py 1` |
| Fałszywe TANKOWANIE / NAGŁY SPADEK po zmianie gęstości lub kalibracji (SW ≤ 0.5.2) | skok przeliczonego poziomu | od 0.5.3 okna liczone od nowa po zmianie |
| Brak zapisu pomiarów po „Usuń logi” (SW ≤ 0.5.2) | sterownik nie zakładał skasowanych katalogów | od 0.5.3 poprawione |
| HTTP 429 „Za dużo zapytań” | Limit 10 zapytań/s dla tokenu API | szybkie odczyty (`blink.py`) przez sesję z logowaniem |
| Odczyt wyższy o ok. 0,07 mA | Błąd toru / zadajnika (znany) | tabela kalibracji prądu (Ustawienia → Przetworniki) |
| Buzer gra po teście | alarm nieskasowany | `sym.py ack 3` |

## 6. Wyniki – 16.11.2026

### Modbus TCP – SW 0.5.4 (15:07–15:20)

| Część | Wynik |
|---|---|
| Odczyty FC04 (urządzenie, zbiornik 1 i 2), FC02, FC01, FC03 – zgodność z API | ✅ 37/37 (poziom 149,1 cm, 14,02 mA, progi, czas ±3 s) |
| Wyjątki 01/02/03, Unit ID 0/1/255 (7 bez odpowiedzi) | ✅ |
| 4 klientów jednocześnie, 5. odrzucony | ✅ |
| Czas odpowiedzi przez Access Point | ✅ 5–15 ms |
| Zapis wyłączony → wyjątek 01 (FC05, FC06) | ✅ |
| `--zapis`: FC06/FC16, odrzucenie złych progów (03), zapis cm → tryb własny i %, cewka wyjścia stałego → 02, przywrócenie | ✅ 8/8 |
| `--dynamika`: MINIMALNY / KRYTYCZNY w rejestrach 103 i FC02, bit alarmu, kasowanie cewką 100 | ✅ (w 1. przebiegu raz niezgodne bity alarmu tuż po zmianie prądu – nie powtórzyło się w zapisie co 0,5 s ani w ponownym teście; odczyt pojedynczy = blokowy) |

Uwagi: zmiana ustawień Modbus na stronie / przez API restartuje serwer Modbus (klienci są rozłączani i muszą połączyć się ponownie). Temperatura DS3231 na płytce: 49,8 °C.

### SW 0.5.4 – podpisane aktualizacje (14:53)

| Plik (`fw-flash ota`) | Wynik |
|---|---|
| bez podpisu (`02_Monitor_App.ino.bin`) | HTTP 400 „Plik bez podpisu MonitorUp.pl”, bez restartu ✅ |
| podpisany, 1 bit zmieniony | HTTP 400 „Nieprawidłowy podpis pliku”, bez restartu ✅ |
| podpisany `MonitorUp_v0.5.4.bin` | wgrany, start 0.5.4 ✅ |

### SW 0.5.3 – kalibracja i karta SD (13:59–14:07)

| Test | Wynik |
|---|---|
| Kalibracja +10 cm (149,1 → 159,1 cm), po 3 min −10 cm | zdarzenia STATYSTYKI + KALIBRACJA, **bez** tankowania i nagłego spadku, stan POPRAWNY ✅ |
| Zapis pomiarów po usunięciu logów (13:29) | 0.5.2: błąd zapisu co 5 min; 0.5.3: zapis 14:00 i 14:05 bez błędów, plik dnia założony od nowa ✅ |

### SW 0.5.2 – buzer w rytmie kontrolki (13:05–13:27)

| Alarm | Czerwona (WY3) | Buzer (WY4) | Wynik |
|---|---|---|---|
| Krytyczny (5,2 mA = 14,5 cm) | ciągle (156/156 próbek) | ciągle | ✅ |
| Przepełnienie (19,8 mA = 197,6 cm) | 1,00×/s (51 %) | 1,00×/s, razem z kontrolką | ✅ |
| Nagły spadek (125,2 → 110,4 cm) | 2,00×/s (54 %) | 2,00×/s | ✅ |
| Usterka czujnika (A4 odłączone, 0,06 mA) | 3,05×/s (50 %) | 3,05×/s; zielona i pomarańczowa zgaszone, WY5 puszcza | ✅ |
| Wyciszenie (API) | miga dalej | wyłączony | ✅ |
| Start z aktywnym IN1 | – | nie wycisza się (0.5.1 wyciszał) | ✅ |

Wykrywanie tankowania (0.5.1): tankowanie zakończone 13:24, przez kolejne 2 min bez nowego – ✅.

### SW 0.5.0 (testy 1–4, 8) i 0.5.1 (test 5)

| Nr | Zadane | Odczyt sterownika | Stan | Wyjścia / miganie | Wynik |
|---|---|---|---|---|---|
| 1 | 14,0 mA | 14,02 mA = 125,2 cm | POPRAWNY | `out` 17: zielona + WY5 | ✅ |
| 2 | 7,6 mA | 7,67 mA = 45,9 cm | MINIMALNY (po 10 s) | `out` 18: pomarańczowa + WY5, ostrzeżenie | ✅ |
| 3 | 5,2 mA | 5,16 mA = 14,5 cm | KRYTYCZNY | `out` 28: czerwona ciągle (146/146 próbek) + buzer | ✅ |
| 4 | 19,8 mA | 19,82 mA = 197,7 cm | ZA DUŻY | czerwona miga **1,00×/s** (49 % czasu) + buzer | ✅ |
| 5 | 14 ↔ 12,8 mA (SW 0.5.1) | 125,2 → 110,4 cm | NAGŁY SPADEK o 13,8 cm (zdarzenie 12:50) | czerwona miga **1,99×/s** (48 % czasu) + buzer; po wyciszeniu buzer wyłączony, miganie trwa | ✅ |
| 6 | 2,0 mA → odłączenie A4 | 3,91 mA (zadajnik nie schodzi niżej); po odłączeniu 0,06 mA | BŁĄD CZUJNIKA | czerwona 3,00×/s, zielona i pomarańczowa zgaszone, WY5 puszcza (0.5.1: buzer ciągle) | ✅ ręcznie |
| 7 | 22,0 mA | 20,06 mA (zadajnik nie daje więcej) | – | – | ⏸ ręcznie |
| 8 | wyciszenie API | – | alarm skasowany | buzer wyłączony (`out` 28 → 20 / 16), czerwona dalej świeci / miga | ✅ |

Uwagi z testu:
- Pierwsza próba testu 5 (SW 0.5.0) nie dała alarmu: po zakończeniu sztucznego „tankowania” z sekwencji testów (+154,8 cm) sterownik w następnej minucie zaczął kolejne – okno 15 min sięgało jeszcze poziomu sprzed zakończenia. W 0.5.1 okno wykrywania tankowania zaczyna się od końca poprzedniego. W normalnej pracy (poziom rośnie tylko przy tankowaniu) to się nie zdarzało.
- Przy teście 5 sterownik po restarcie (aktualizacja) odtworzył historię z karty SD i wykrył spadek 125 → 110 cm sprzed restartu – zgodnie z założeniem.
- Na koniec: alarm spadku wyłączony i włączony (skasowanie zatrzaśnięcia), 14 mA, stan POPRAWNY.
- Pierwszy przebieg testów 3–4 przy włączonym alarmie spadku pokazał NAGŁY SPADEK (zatrzaśnięty po spadku 46 → 15 cm) – zachowanie zgodne z założeniem, stąd kolejność w punkcie 4.
