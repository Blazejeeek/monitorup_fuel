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
4. Podłączyć sterownik kablem Ethernet do sieci biura z dostępem do internetu (test połączenia z chmurą).
5. Sprawdzić gotowość: `produkcja.py check` – baza, Zdalio (rejestr urządzeń i serwer chmury), sterownik i drukarka muszą odpowiadać.

## 4. Produkcja egzemplarza

Uruchomić: `produkcja.py make --partia <oznaczenie partii>` (opcjonalnie `--skladniki` z numerami faktur płytki, czujnika, zasilacza).

| Krok | Co robi stanowisko | Co robi operator |
|---|---|---|
| 1 | odczytuje MAC i wersję oprogramowania przez USB | – |
| 2 | pobiera kolejny numer z rejestru, losuje hasła (Wi-Fi, panel, chmura) | – |
| 2a | zakłada konto sterownika na serwerze chmury i rejestruje go w Zdalio; Zdalio nadaje kod aktywacji | – |
| 3 | zapisuje dane fabryczne w sterowniku, sprawdza je odczytem kontrolnym | – |
| 4 | drukuje naklejkę wewnętrzną 50×40: numer, ID i kod aktywacji Zdalio z kodem QR, hasła Wi-Fi i panelu | odłożyć naklejkę przy sterowniku |
| 5 | test: układy płytki, wyjścia OUT1–OUT8 po kolei | potwierdzić, że zapaliły się kolejno wszystkie wyjścia |
| 6 | test: wejście DI1 | w ciągu 20 s nacisnąć przycisk kasowania |
| 7 | test 4–20 mA w 5 punktach (4, 8, 12, 16, 20 mA), zapis kalibracji fabrycznej, kontrola przy 12 mA (±0,05 mA) | – |
| 7a | test połączenia z chmurą (do 3 min); chmura zostaje wyłączona – włącza ją instalator | – |
| 8 | drukuje tabliczkę znamionową (tylko po zaliczonym teście) | nakleić tabliczkę na obudowę, naklejkę wewnętrzną – pod pokrywę |

Wynik: komunikat **GOTOWE: MU-…** i stan **gotowy**.

**Błędy:**

- Brak potwierdzenia zapisu (krok 3) – skrypt sam resetuje sterownik i rozstrzyga odczytem kontrolnym; przy zgodnym odczycie produkcja idzie dalej.
- Błąd rejestracji w Zdalio, zapisu lub odczytu kontrolnego (kroki 2–3) – wpis zostaje anulowany automatycznie, a konto chmury i wpis w Zdalio usunięte. Usunąć przyczynę i uruchomić `produkcja.py make --ponownie` (sterownik dostanie nowy numer; anulowany numer zostaje w historii).
- Błąd drukarki przy naklejce z hasłami (krok 4) – usunąć usterkę, potem `produkcja.py serwis-hasla MU-… --powod "błąd druku"` (nowe hasła i nowa naklejka) i `produkcja.py test MU-…`.
- Test niezaliczony (kroki 5–7) – egzemplarz zostaje w stanie **wyprodukowany**, tabliczka nie jest drukowana, kalibracja nie jest zapisywana. Po naprawie: `produkcja.py test MU-…` (test, kalibracja, tabliczka).
- Test niepełny (bez operatora: `--bez-wyjsc`, `--bez-di`; bez zadajnika: `--bez-pradu`; bez sieci biura: `--bez-chmury`) – pominięte części są zapisane w teście, egzemplarz zostaje w stanie **wyprodukowany**, kalibracja z tego testu jest zachowana. Dokończenie: `produkcja.py test MU-…` z operatorem (wydrukuje tabliczkę).
- Ponowny test egzemplarza z kalibracją fabryczną sprawdza wejście w 3 punktach i nie zmienia tabeli; wymiana tabeli: `produkcja.py test MU-… --nowa-kalibracja`.
- Sterownik ma już numer aktywny w rejestrze – nie programować ponownie; sprawdzić w rejestrze (`rejestr.py szukaj MU-…`).

## 5. Wydanie klientowi

1. Sprawdzić stan: `rejestr.py szukaj MU-…` – musi być **gotowy**.
2. Zapisać wydanie: `rejestr.py wydaj MU-… --dokument "WZ 12/11/2026" --odbiorca "Nazwa firmy"`.
3. Przekazać klientowi sterownik z naklejką wewnętrzną. ID urządzenia i kod aktywacji (albo kod QR) z naklejki służą do dodania sterownika w aplikacji Zdalio – działają tylko przy pierwszym dodaniu.

Przy pierwszym logowaniu do panelu (login **admin**, hasło startowe z naklejki) sterownik wymusza zmianę hasła; do tego czasu panel działa tylko w trybie podglądu.

## 6. Serwis

| Sytuacja | Czynność |
|---|---|
| Zwrot, naprawa | `rejestr.py stan MU-… serwis --powod "…"`, opis prac: `rejestr.py uwaga MU-… "…" --serwis`, po naprawie i teście (`produkcja.py test MU-…`) – stan **gotowy** albo `stan … wydany` |
| Klient zapomniał hasła panelu | przycisk DOWNLOAD na płytce przez 10 s – hasła wracają do startowych z naklejki |
| Zgubiona naklejka wewnętrzna | podłączyć sterownik do stanowiska: `produkcja.py serwis-hasla MU-… --powod "zgubiona naklejka"` – nowe hasła i nowy kod aktywacji (stare unieważnione w rejestrze i w Zdalio), nowa naklejka |
| Kradzież, nadużycie | `rejestr.py stan MU-… zablokowany --powod "…"` |
| Złom, wycofanie | `rejestr.py stan MU-… wycofany --powod "…"` |
| Administrator sieci klienta podaje MAC | `rejestr.py szukaj <MAC>` – numer, odbiorca, historia |

Ustawienia fabryczne i reset DOWNLOAD nie kasują danych fabrycznych (numer, hasła startowe, kalibracja fabryczna). Numer seryjny ma dodatkowo kopię w pamięci EEPROM płytki.

Wyczyszczenie całej pamięci sterownika (np. wgranie pełnego obrazu przez USB) usuwa dane fabryczne – zostaje tylko numer z EEPROM, a sterownik startuje z hasłami admin / monitor123. Przywrócenie: `produkcja.py serwis-hasla MU-… --powod "utrata danych fabrycznych"`, potem `produkcja.py test MU-…` (nowa kalibracja).

## 7. Oprogramowanie

- Każda wydana wersja oprogramowania trafia do rejestru z sumą SHA-256: `rejestr.py dodaj-fw MU <wersja> <plik> --podpisany`.
- Liczba egzemplarzy z daną wersją: `rejestr.py firmware`.
- Aktualizacje przez sieć (strona sterownika i chmura Zdalio) przyjmują tylko pliki podpisane przez GlobalSmart. Z chmury nie da się wgrać wersji starszej niż najstarsza z obsługą chmury (0.6.0).

## 8. Kopia rejestru

Rejestr jest kopiowany codziennie (30 ostatnich kopii). Przeniesienie na inny serwer – odtworzenie ostatniej kopii i zmiana adresu bazy w ustawieniach stanowiska.
