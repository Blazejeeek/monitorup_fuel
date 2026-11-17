# MonitorUp.pl – Instrukcja obsługi i montażu

**Sterownik monitoringu poziomu cieczy w zbiorniku** · sprzęt HW 1.0 · oprogramowanie SW 0.5.5 (docelowe)

*Wersja robocza 0.1 z 17.11.2026.* Sygnalizacja opisana zgodnie z nakładką panelu przyjętą 16.11.2026 – zostanie wprowadzona w oprogramowaniu 0.5.5. Pozycje oznaczone **[do uzupełnienia]** wymagają danych producenta.

---

## 1. Przeznaczenie

MonitorUp.pl jest sterownikiem do ciągłego pomiaru poziomu cieczy w zbiorniku (olej napędowy, woda, inne ciecze) za pomocą przetwornika hydrostatycznego 4–20 mA. Sterownik:

- mierzy poziom i objętość cieczy,
- sygnalizuje stany alarmowe kontrolkami, sygnałem akustycznym i stykiem usterki,
- wykrywa przepełnienie, poziom minimalny i krytyczny, nagły spadek poziomu (możliwy wyciek lub pobór nieuprawniony) oraz awarię czujnika,
- rejestruje pomiary, zużycie i tankowania na karcie pamięci,
- udostępnia dane systemom nadrzędnym (BMS, SCADA) przez Modbus TCP, MQTT i API.

Typowe zastosowania: zbiorniki paliwa agregatów prądotwórczych, zbiorniki ppoż. wody i paliwa, zbiorniki technologiczne.

## 2. Bezpieczeństwo

- Montaż i podłączenie może wykonywać wyłącznie osoba z odpowiednimi uprawnieniami elektrycznymi.
- Wszystkie podłączenia wykonywać przy odłączonym zasilaniu.
- Sterownik zasilać wyłącznie napięciem stałym 12–24 V DC z zasilacza z separacją galwaniczną.
- W strefach zagrożonych wybuchem (paliwa) stosować przetwornik i bariery zgodne z dokumentacją strefy Ex. Sam sterownik montować poza strefą zagrożenia.
- Urządzenie przeznaczone jest do pracy w sieci lokalnej. Nie udostępniać go bezpośrednio w internecie.
- Sterownik nie zastępuje systemów zabezpieczeń wymaganych przepisami (np. sygnalizacji przecieku lub automatyki pożarowej).

## 3. Panel i sygnalizacja

Panel w drzwiach obudowy zawiera trzy kontrolki stanu, przycisk kasowania alarmu, legendę sygnalizacji i schemat stref zbiornika.

### 3.1 Kontrolki

| Kontrolka | Znaczenie ogólne |
|---|---|
| **NORMA** (zielona) | praca bez zakłóceń, poziom w zakresie normalnym |
| **OSTRZEŻENIE** (pomarańczowa) | stan wymagający zaplanowania działania |
| **ALARM** (czerwona) | stan wymagający natychmiastowego działania |

### 3.2 Stany i zalecane działania

| Kontrolka | Światło | Stan | Zalecane działanie |
|---|---|---|---|
| NORMA | ciągłe | Stan normalny | brak |
| OSTRZEŻENIE | ciągłe | Poziom minimalny | zaplanować uzupełnienie |
| OSTRZEŻENIE | przerywane | Awaria czujnika | wezwać serwis |
| ALARM | ciągłe | Poziom krytyczny | uzupełnić zbiornik |
| ALARM | przerywane | Przepełnienie | wstrzymać napełnianie |
| ALARM | przerywane szybkie | Nagły spadek poziomu | sprawdzić szczelność |

Światło przerywane – ok. 1 błysk na sekundę; przerywane szybkie – ok. 4 błyski na sekundę.

![Panel sterownika – nakładka na drzwi obudowy](panel_skrot.jpg)

![Sygnalizacja stanów](stany.jpg)

### 3.3 Sygnał akustyczny i kasowanie alarmu

- Sygnalizator akustyczny (buzer) włącza się przy stanach ALARM i pracuje w rytmie czerwonej kontrolki. **[do potwierdzenia]**
- Przycisk **KASOWANIE ALARMU** wycisza sygnalizator i potwierdza alarm. Kontrolka ALARM świeci dalej, dopóki przyczyna nie ustąpi.
- Wystąpienie nowego, poważniejszego stanu ponownie włącza sygnalizator.
- Alarm można też skasować ze strony WWW, przez Modbus, MQTT lub API.
- Tryb kasowania (ręczny lub automatyczny po powrocie poziomu do normy) oraz przypomnienie po zadanym czasie ustawia się w konfiguracji (rozdz. 7.6).

### 3.4 Styk usterki zbiorczej

Wyjście WY5 steruje przekaźnikiem usterki zbiorczej. Przekaźnik jest **zasilony, gdy nie ma usterki** – zanik zasilania sterownika również powoduje sygnał usterki w systemie nadrzędnym. Przyczyny usterki wybiera się w konfiguracji (domyślnie: awaria czujnika, przetwornika A/C, karty pamięci).

### 3.5 Wyświetlacz

Wyświetlacz OLED na płytce pokazuje na przemian:

- **ekran zbiornika** – poziom [cm], wypełnienie [%], objętość [l], stan i słupek z progami,
- **ekran sieci** – adresy Ethernet, Wi-Fi i punktu dostępowego, wersje SW/HW.

## 4. Obsługa codzienna

1. **NORMA świeci** – nie są wymagane żadne czynności.
2. **OSTRZEŻENIE świeci** – zaplanować dostawę cieczy; stan ustąpi samoczynnie po uzupełnieniu.
3. **OSTRZEŻENIE miga** – pomiar jest niewiarygodny; powiadomić serwis. Alarmy poziomu nie działają do czasu usunięcia awarii.
4. **ALARM świeci lub miga** – nacisnąć KASOWANIE ALARMU, aby wyciszyć sygnalizator, i wykonać działanie z tabeli w rozdz. 3.2. Powiadomić osobę odpowiedzialną za obiekt.
5. Bieżący poziom, zużycie i historię można sprawdzić na stronie WWW sterownika (rozdz. 6).

## 5. Montaż i podłączenie

### 5.1 Montaż

- Sterownik montuje się na szynie DIN 35 mm w obudowie elektrycznej, z kontrolkami i przyciskiem w drzwiach (otwory ⌀ 22,3 mm, wg nakładki panelu).
- Wymiary modułu: 122 × 83 × 59 mm.
- Antenę Wi-Fi (jeśli używana) wyprowadzić na zewnątrz metalowej obudowy.

### 5.2 Zaciski

| Zacisk | Funkcja | Uwagi |
|---|---|---|
| Zasilanie + / – | 12–24 V DC | napięcie zasilania = napięcie wyjść |
| A4 | wejście przetwornika 4–20 mA | rezystor pomiarowy 150 Ω |
| OUT1 (WY1) | kontrolka NORMA | wyjście tranzystorowe, napięcie zasilania |
| OUT2 (WY2) | kontrolka OSTRZEŻENIE | j.w. |
| OUT3 (WY3) | kontrolka ALARM | j.w. |
| OUT4 (WY4) | sygnalizator akustyczny | j.w. |
| OUT5 (WY5) | przekaźnik usterki zbiorczej | zasilony = brak usterki |
| DI1 | przycisk KASOWANIE ALARMU | styk bezpotencjałowy, NO |
| RJ45 | Ethernet 10/100 Mb/s | sieć lokalna / BMS |

- Wyjścia OUT pracują napięciem zasilania sterownika. Zalecany prąd ciągły ≤ 2 A na wyjście; większe obciążenia i obwody 230 V podłączać przez przekaźnik pośredniczący.
- Na wejścia DI nie wolno podawać zewnętrznego napięcia.

### 5.3 Przetwornik poziomu

- Przetwornik 2-przewodowy 4–20 mA zasilić z obwodu 12–24 V DC sterownika; przewód sygnałowy podłączyć do zacisku A4. **[do uzupełnienia: schemat podłączenia]**
- Stosować kabel ekranowany; ekran uziemić jednostronnie po stronie sterownika.
- Sondę opuścić na dno zbiornika (lub do znanej wysokości) – wysokość zawieszenia uwzględnia się w kalibracji.
- Prąd poniżej 3,6 mA lub powyżej 21 mA sterownik traktuje jako awarię czujnika.

## 6. Pierwsze uruchomienie

1. Po włączeniu zasilania wyświetlacz pokazuje logo, wersje SW/HW, a sterownik krótko testuje wyjścia (zapalają się kolejno kontrolki).
2. Połączyć się z siecią Wi-Fi sterownika **MonitorUp-XXXX** (hasło fabryczne `monitor123`) i otworzyć `http://192.168.4.1`. Przy podłączonym Ethernecie z DHCP adres sterownika widać na ekranie sieci wyświetlacza.
3. Zalogować się: użytkownik `admin`, hasło `admin`. Sterownik wymusza zmianę hasła przy pierwszym logowaniu.
4. W **Ustawienia → Sieć** ustawić Ethernet lub Wi-Fi (DHCP lub adres stały) oraz tryb punktu dostępowego (zawsze / awaryjnie / wyłączony). Zmiana sieci powoduje restart.
5. W **Ustawienia → Data i czas** sprawdzić datę i godzinę (zegar RTC z podtrzymaniem, synchronizacja NTP).
6. Skonfigurować zbiornik, czujnik i alarmy (rozdz. 7), a następnie wykonać kalibrację.

## 7. Konfiguracja

### 7.1 Zbiornik (Ustawienia → Przetworniki)

- Wysokość zbiornika [cm].
- Objętość (opcjonalnie): walec leżący, walec stojący, prostopadłościan lub tabela przeliczeniowa cm → litry (np. z tabeli legalizacyjnej zbiornika).

### 7.2 Czujnik (Ustawienia → Przetworniki)

- Zakres przetwornika (poziom dla 4 mA i 20 mA) wyrażony jako słup wody.
- Gęstość cieczy: woda 1000 kg/m³, olej napędowy ok. 840 kg/m³ – sterownik przelicza zakres na wysokość cieczy.
- Tabela kalibracji prądu (opcjonalnie, do 24 punktów) – dla instalacji wymagających podwyższonej dokładności.

### 7.3 Kalibracja

1. Zmierzyć rzeczywisty poziom cieczy miarką (prętem) [cm].
2. W **Ustawienia → Przetworniki → Kalibracja** wpisać zmierzoną wartość w polu „Aktualny poziom cieczy” i wybrać **Kalibruj**.

Sterownik koryguje przesunięcie sondy; zakres i gęstość pozostają bez zmian. Kalibracja dwupunktowa (MIN/MAX) jest dostępna w opcjach zaawansowanych. Kalibracja jest zachowywana przy aktualizacji i przywracaniu ustawień fabrycznych.

### 7.4 Progi alarmowe (Ustawienia → Alarmy)

| Tryb | Poziom krytyczny | Poziom minimalny | Przepełnienie |
|---|---|---|---|
| Zbiornik ppoż. wody | ≤ 90 % | ≤ 95 % | ≥ 98 % |
| Zbiornik ppoż. paliwa | ≤ 80 % | ≤ 90 % | ≥ 95 % |
| Własne | wg ustawień | wg ustawień | wg ustawień |

Progi wyrażone są w % wysokości zbiornika (podgląd w cm). Fabrycznie: histereza 2 cm, opóźnienie zadziałania 10 s. **[do potwierdzenia: progi trybów ppoż.]**

### 7.5 Nagły spadek poziomu (Ustawienia → Statystyki)

Alarm zadziała, gdy poziom spadnie o zadaną wartość w zadanym czasie (fabrycznie 3 cm w 10 min). W oknie nocnym (fabrycznie 22:00–6:00) obowiązuje czulszy próg (1 cm). Tankowanie jest rozpoznawane automatycznie i nie powoduje fałszywych alarmów.

### 7.6 Kasowanie i usterka zbiorcza

- **Kasowanie** (Ustawienia → Alarmy → Buzer i kasowanie): ręczne (przycisk, strona, Modbus, MQTT, API; opcjonalne przypomnienie po X min) lub automatyczne – alarm gaśnie po powrocie poziomu do normy.
- **Usterka zbiorcza:** w **Ustawienia → Wyjścia** wybrać przyczyny (awaria czujnika, przetwornika A/C, karty pamięci, brak synchronizacji czasu, brak sieci, brak połączenia MQTT); podgląd stanu na żywo.

## 8. Rejestracja danych

- Pomiary zapisywane są na karcie microSD (FAT32) co 5 min i przy każdej zmianie stanu; przechowywanie fabrycznie 10 lat.
- Zakładka **Logi**: wykresy dzień / miesiąc / rok / wszystkie lata, zużycie, tankowania, dziennik zdarzeń, pobieranie plików CSV.
- Brak lub uszkodzenie karty nie zatrzymuje pomiaru ani alarmów (ostatnie 24 h dostępne w pamięci sterownika).

## 9. Integracja z systemami nadrzędnymi

| Interfejs | Parametry | Uwagi |
|---|---|---|
| Modbus TCP | port 502 | fabrycznie wyłączony; zapis fabrycznie wyłączony; mapa rejestrów do pobrania ze strony sterownika |
| MQTT | broker w sieci lokalnej | publikacja stanu, poziomu i zdarzeń, kasowanie alarmu |
| API HTTP | `/api/v1`, tokeny Bearer | uprawnienia, termin ważności i limit zapytań tokenu |

Opis rejestrów, tematów i zapytań z przykładami dostępny jest w **Ustawienia → API** oraz w dokumentach MODBUS.md i API.md. Transmisja HTTP, Modbus i MQTT nie jest szyfrowana – stosować wyłącznie w wydzielonej sieci lokalnej.

## 10. Konserwacja i serwis

- **Kontrola okresowa** (zalecana co 12 miesięcy): sprawdzenie kalibracji pomiarem miarką, test kontrolek i sygnalizatora, test styku usterki, kontrola karty pamięci.
- **Aktualizacja oprogramowania:** **Ustawienia → System → Aktualizacja**, plik `MonitorUp_vX.Y.Z.bin`. Sterownik przyjmuje wyłącznie pliki podpisane przez producenta; plik niepodpisany lub uszkodzony jest odrzucany, a dotychczasowa wersja pozostaje. Ustawienia i kalibracja są zachowywane.
- **Kopia ustawień:** **Ustawienia → System → Kopia ustawień**; kopia z hasłami jest szyfrowana hasłem kopii.
- **Zapomniane hasło:** przytrzymać przycisk DOWNLOAD na płytce przez 10 s (odliczanie na wyświetlaczu) – przywracane są fabryczne login, hasło i ustawienia sieci; kalibracja pozostaje.
- **Ustawienia fabryczne:** **Ustawienia → System → Restart i ustawienia fabryczne** (kalibracja pozostaje).
- **Zmiana hasła:** **Ustawienia → Konto**.

## 11. Usuwanie usterek

| Objaw | Możliwa przyczyna | Działanie |
|---|---|---|
| OSTRZEŻENIE miga | przerwa lub zwarcie w obwodzie czujnika, uszkodzony przetwornik | sprawdzić przewody i zasilanie przetwornika; wezwać serwis |
| Odczyt poziomu różni się od miarki | przesunięcie sondy, zła gęstość cieczy | sprawdzić gęstość, wykonać kalibrację (rozdz. 7.3) |
| Brak dostępu do strony | zmieniony adres IP, brak sieci | odczytać IP z ekranu sieci; połączyć się przez Wi-Fi sterownika |
| Brak zapisu logów | brak lub uszkodzona karta pamięci | Ustawienia → Logi → Test karty SD; wymienić kartę (FAT32) |
| Fałszywe alarmy nagłego spadku | zbyt czuły próg, pobór technologiczny | zwiększyć próg lub czas w ustawieniach nagłego spadku |
| Styk usterki aktywny | przyczyna z listy usterki zbiorczej | sprawdzić stan na stronie: Ustawienia → Wyjścia |

## 12. Dane techniczne

| Parametr | Wartość |
|---|---|
| Zasilanie | 12–24 V DC |
| Pobór mocy | **[do uzupełnienia]** |
| Wejście pomiarowe | 4–20 mA, 16 bit, rezystor 150 Ω; awaria < 3,6 mA lub > 21 mA |
| Okres pomiaru | próbkowanie 100 ms, wynik co 1 s |
| Wyjścia | 5 × tranzystorowe (MOSFET), napięcie zasilania, ≤ 2 A ciągłego na wyjście |
| Wejście kasowania | styk bezpotencjałowy, izolacja optoelektroniczna |
| Komunikacja | Ethernet 10/100 (RJ45), Wi-Fi 2,4 GHz, Modbus TCP, MQTT, HTTP API |
| Rejestracja | karta microSD (FAT32), do 10 lat pomiarów |
| Zegar | RTC z podtrzymaniem bateryjnym, synchronizacja NTP |
| Wyświetlacz | OLED 128 × 64 |
| Montaż | szyna DIN 35 mm |
| Wymiary modułu | 122 × 83 × 59 mm |
| Temperatura pracy | **[do uzupełnienia]** |
| Stopień ochrony | wg obudowy **[do uzupełnienia]** |

---

Producent: **[do uzupełnienia]** · www.monitorup.pl
