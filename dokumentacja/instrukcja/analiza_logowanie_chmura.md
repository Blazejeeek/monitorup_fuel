# MonitorUp.pl – Logowanie do chmury

**Porównanie modeli wgrywania danych logowania na produkcji i rekomendacja**

*Analiza wewnętrzna, wersja 0.1 z 18.11.2026. Dane o producentach z wiedzy ogólnej – do potwierdzenia przed decyzją.*

---

## 1. Jak robią to inni producenci

| Producent / platforma | Dane logowania urządzenia | Kiedy powstają |
|---|---|---|
| Tuya | unikalna licencja: identyfikator + klucz | wgrywana na produkcji narzędziem fabrycznym |
| Shelly | konto chmurowe; osobno lokalne MQTT (login i hasło) | chmura po sparowaniu; osobne przełączniki „Chmura” i „MQTT” |
| AWS IoT Core | certyfikat X.509 na każde urządzenie (mTLS) | na produkcji albo „fleet provisioning” przy pierwszym połączeniu |
| Azure IoT Hub | certyfikat X.509, TPM albo token SAS (z datą ważności) | usługa rejestracji (DPS) przy pierwszym połączeniu |
| Google Cloud IoT (zamknięty 2023) | urządzenie podpisywało token JWT własnym kluczem | klucz generowany w urządzeniu |
| Espressif RainMaker | certyfikat X.509 | na produkcji albo przy pierwszym uruchomieniu |
| Tasmota / ESPHome | login i hasło MQTT, opcjonalnie TLS | wpisuje użytkownik |

## 2. Porównanie modeli

| Kryterium | A. Unikalne hasło z produkcji | B. Token JWT z produkcji | C. Dane startowe → unikalne przy 1. połączeniu | D. Certyfikat X.509 / klucz w urządzeniu |
|---|---|---|---|---|
| Przykłady | Tuya, Shelly | Azure (SAS), Google IoT | AWS fleet provisioning, Azure DPS, RainMaker | AWS IoT, Azure, przemysł |
| Produkcja | konto zakładane na brokerze, wgranie loginu i hasła | token podpisany lokalnie na stanowisku | ten sam plik startowy dla wszystkich płytek | podpisanie certyfikatu lub klucz generowany w płytce |
| Internet na stanowisku | tak (API brokera) | nie | nie | zależnie od wariantu |
| Uprawnienia do tematów | ACL brokera | zapisane w tokenie | nadawane przy rejestracji | ACL brokera |
| Ważność | bezterminowo | z datą (np. 1–2 lata) | zależnie od wariantu | z datą (lata) |
| Odnawianie | niepotrzebne | potrzebne (usługa tokenów) | zależnie od wariantu | przed wygaśnięciem |
| Blokada zgubionego urządzenia | usunięcie konta – 1 klik | czarna lista lub krótka ważność | po stronie serwera | odwołanie certyfikatu |
| Wyciek danych z jednej płytki | tylko to urządzenie | tylko to urządzenie | ryzyko rejestracji fałszywych urządzeń | tylko to urządzenie |
| Bezpieczeństwo | ★★★ | ★★★ | ★★★ | ★★★★★ |
| Zmiany w sterowniku | małe | małe + odnawianie tokenu | średnie | duże |
| Zmiany po stronie serwera | ACL + izolacja tematów | JWT w EMQX + usługa odnawiania | serwis rejestracji | własny urząd certyfikacji + ACL |
| Praca na start | mała | średnia | średnia / duża | duża |
| Skala | do kilkuset szt., produkcja u siebie | setki–tysiące szt. | produkcja u podwykonawcy | tysiące+, przemysł |
| Dalsza ścieżka | → B lub C bez wymiany sprzętu | → D | → D | – |

Odczyt danych z płytki przez USB jest możliwy w każdym modelu, dopóki pamięć urządzenia nie jest szyfrowana (eFuse – świadomie wyłączone).

## 3. Rekomendacja dla MonitorUp.pl

| Etap | Model | Uzasadnienie |
|---|---|---|
| Teraz (SW 0.5.6) | A – unikalne hasło z produkcji, przełącznik „Chmura MonitorUp.pl wł./wył.”, osobne MQTT dla BMS | najmniej pracy, standard rynkowy, produkcja u siebie |
| Większa liczba sztuk | B albo C | stanowisko bez dostępu do brokera, produkcja u podwykonawcy |
| Wymagania przemysłowe / przetargi | D + szyfrowanie pamięci | najwyższy poziom bezpieczeństwa, nieodwracalne ustawienia płytki |

## 4. Proponowany proces produkcyjny (model A)

1. Podłączenie sterownika przez USB do stanowiska (Pi lub PC) i uruchomienie skryptu produkcyjnego.
2. Odczyt identyfikatora płytki (MAC), nadanie numeru seryjnego (np. `MU-RRMM-NNNN`).
3. Generowanie unikalnego hasła MQTT, założenie konta na brokerze z uprawnieniami tylko do tematów urządzenia.
4. Zapis do chronionej pamięci sterownika: numer seryjny, dane chmury, unikalne hasło administratora.
5. Test końcowy: połączenie z chmurą, wyjścia, wejście 4–20 mA.
6. Wydruk naklejki (numer seryjny, kod QR, hasło startowe) na drukarce PT-80B.
7. Wpis do rejestru produkcji: numer seryjny, MAC, wersja oprogramowania, data, wynik testu.

Dane fabryczne nie są kasowane przez „ustawienia fabryczne”, nie trafiają do kopii ustawień i są widoczne na stronie tylko do odczytu.

## 5. Warunek wstępny – broker EMQX

| Zadanie | Stan obecny | Wymagane |
|---|---|---|
| Izolacja tematów (ACL) | brak – każdy klient widzi wszystkie tematy | każde urządzenie tylko swoje tematy |
| Certyfikat TLS (port 40022) | demonstracyjny, publicznie znany klucz | prawdziwy certyfikat (np. Let's Encrypt) |
| Panel administracyjny | zwykłe HTTP | dostęp tylko z serwera lub przez HTTPS |

## 6. Decyzje do podjęcia

| Nr | Pytanie | Propozycja |
|---|---|---|
| 1 | Dwa niezależne połączenia: chmura MonitorUp + MQTT dla BMS | tak |
| 2 | Rejestr produkcji | Supabase (pod przyszły panel MonitorUp); alternatywa: CSV w Nextcloud |
| 3 | Format numeru seryjnego | `MU-RRMM-NNNN` |
| 4 | Zakres pierwszego etapu | SW 0.5.6 + skrypt produkcyjny + naklejka; zabezpieczenie brokera jako osobny plan |
