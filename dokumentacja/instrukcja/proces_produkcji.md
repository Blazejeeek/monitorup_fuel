# MonitorUp.pl – Proces produkcji i wydawania

**Instrukcja stanowiskowa: od zmontowanego sterownika do wydania klientowi i serwisu**

*Oprogramowanie SW 0.5.6. Wersja robocza 0.1 z 19.11.2026. Dokument wewnętrzny GlobalSmart.*

---

## 1. Zasady

- Każdy egzemplarz ma numer seryjny **MU-RRMM-NNNN** (RR rok, MM miesiąc produkcji, NNNN numer kolejny w miesiącu). Numer nadaje rejestr produkcji – nie nadawać numerów ręcznie poza płytkami testowymi.
- Wszystkie czynności zapisują się w rejestrze produkcji automatycznie. Dziennika nie da się zmienić ani usunąć – pomyłki prostować nowym wpisem (uwaga, serwis).
- Hasła egzemplarza losuje stanowisko. Jawne hasła są wyłącznie na naklejce wewnętrznej; rejestr przechowuje tylko ich skróty. Naklejki wewnętrznej nie da się wydrukować ponownie – zgubiona naklejka oznacza nowe hasła (rozdz. 6).
- Rejestr produkcji działa tylko w sieci lokalnej biura.

## 2. Stany egzemplarza

| Stan | Znaczenie | Kto ustawia |
|---|---|---|
| wyprodukowany | dane fabryczne zapisane, test jeszcze niezaliczony | stanowisko |
| gotowy | test zaliczony, egzemplarz na magazynie | stanowisko (test) |
| wydany | wydany odbiorcy na dokument WZ/FV | biuro |
| serwis | zwrot, naprawa | biuro / serwis |
| zablokowany | kradzież, nadużycie | biuro |
| wycofany | wycofany na stałe, złom | biuro |
| anulowany | produkcja przerwana; numer zostaje w historii, nie jest używany ponownie | stanowisko (automatycznie) |

## 3. Przygotowanie stanowiska

1. Podłączyć sterownik do komputera stanowiska przez **separator USB** (bez separatora możliwa pętla masy z zasilaczem sterownika).
2. Podłączyć zadajnik 4–20 mA do wejścia A4 sterownika, przycisk kasowania do DI1, kontrolki i sygnalizator do wyjść.
3. Włączyć drukarkę etykiet PT-80B; założyć etykiety 50×40 mm.
4. Sprawdzić gotowość: `produkcja.py check` – baza, sterownik i drukarka muszą odpowiadać.

## 4. Produkcja egzemplarza

Uruchomić: `produkcja.py make --partia <oznaczenie partii>` (opcjonalnie `--skladniki` z numerami faktur płytki, czujnika, zasilacza).

| Krok | Co robi stanowisko | Co robi operator |
|---|---|---|
| 1 | odczytuje MAC i wersję oprogramowania przez USB | – |
| 2 | pobiera kolejny numer z rejestru, losuje hasła (Wi-Fi, panel, chmura, kod aktywacji) | – |
| 3 | zapisuje dane fabryczne w sterowniku, sprawdza je odczytem kontrolnym | – |
| 4 | test: układy płytki, wyjścia OUT1–OUT8 po kolei | potwierdzić, że zapaliły się kolejno wszystkie wyjścia |
| 5 | test: wejście DI1 | w ciągu 20 s nacisnąć przycisk kasowania |
| 6 | test 4–20 mA w 5 punktach (4, 8, 12, 16, 20 mA), zapis kalibracji fabrycznej, kontrola przy 12 mA (±0,05 mA) | – |
| 7 | drukuje tabliczkę znamionową i naklejkę wewnętrzną | nakleić tabliczkę na obudowę, naklejkę wewnętrzną – pod pokrywę |

Wynik: komunikat **GOTOWE: MU-…** i stan **gotowy**.

**Błędy:**

- Błąd zapisu lub odczytu kontrolnego (kroki 2–3) – wpis zostaje anulowany automatycznie. Usunąć przyczynę i uruchomić `make` ponownie (sterownik dostanie nowy numer).
- Test niezaliczony (kroki 4–6) – egzemplarz zostaje w stanie **wyprodukowany**, naklejki nie są drukowane. Po naprawie: `produkcja.py test MU-…`.
- Sterownik ma już numer – nie programować ponownie; sprawdzić w rejestrze (`rejestr.py szukaj MU-…`).

## 5. Wydanie klientowi

1. Sprawdzić stan: `rejestr.py szukaj MU-…` – musi być **gotowy**.
2. Zapisać wydanie: `rejestr.py wydaj MU-… --dokument "WZ 12/11/2026" --odbiorca "Nazwa firmy"`.
3. Przekazać klientowi sterownik z naklejką wewnętrzną. Kod aktywacji służy do przypisania urządzenia w aplikacji MonitorUp.pl.

Przy pierwszym logowaniu do panelu (login **admin**, hasło startowe z naklejki) sterownik wymusza zmianę hasła; do tego czasu panel działa tylko w trybie podglądu.

## 6. Serwis

| Sytuacja | Czynność |
|---|---|
| Zwrot, naprawa | `rejestr.py stan MU-… serwis --powod "…"`, opis prac: `rejestr.py uwaga MU-… "…" --serwis`, po naprawie i teście (`produkcja.py test MU-…`) – stan **gotowy** albo `stan … wydany` |
| Klient zapomniał hasła panelu | przycisk DOWNLOAD na płytce przez 10 s – hasła wracają do startowych z naklejki |
| Zgubiona naklejka wewnętrzna | podłączyć sterownik do stanowiska: `produkcja.py serwis-hasla MU-… --powod "zgubiona naklejka"` – nowe hasła, stare unieważnione w rejestrze, nowa naklejka |
| Kradzież, nadużycie | `rejestr.py stan MU-… zablokowany --powod "…"` |
| Złom, wycofanie | `rejestr.py stan MU-… wycofany --powod "…"` |
| Administrator sieci klienta podaje MAC | `rejestr.py szukaj <MAC>` – numer, odbiorca, historia |

Ustawienia fabryczne i reset DOWNLOAD nie kasują danych fabrycznych (numer, hasła startowe, kalibracja fabryczna). Numer seryjny ma dodatkowo kopię w pamięci EEPROM płytki.

## 7. Oprogramowanie

- Każda wydana wersja oprogramowania trafia do rejestru z sumą SHA-256: `rejestr.py dodaj-fw MU <wersja> <plik> --podpisany`.
- Liczba egzemplarzy z daną wersją: `rejestr.py firmware`.
- Aktualizacje przez sieć przyjmują tylko pliki podpisane przez GlobalSmart.

## 8. Kopia rejestru

Rejestr jest kopiowany codziennie (30 ostatnich kopii). Przeniesienie na inny serwer – odtworzenie ostatniej kopii i zmiana adresu bazy w ustawieniach stanowiska.
