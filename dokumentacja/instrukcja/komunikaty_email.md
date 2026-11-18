# MonitorUp.pl – Komunikaty e-mail

**Wykaz wiadomości wysyłanych przez sterownik – temat, treść, warunek wysłania**

*Oprogramowanie SW 0.5.5. Wersja robocza 0.1 z 18.11.2026.*

---

## 1. Zasady ogólne

- Wiadomości wysyła sterownik przez serwer SMTP skonfigurowany w **Ustawienia → Powiadomienia**.
- Każdy z maksymalnie 5 odbiorców otrzymuje tylko te rodzaje wiadomości, które zostały przy nim zaznaczone.
- Ten sam rodzaj wiadomości jest wysyłany najwyżej raz na ustawiony czas (fabrycznie 15 min). Nie dotyczy wiadomości testowej.
- Nieudana wysyłka jest ponawiana po 1, 5, 15 i 60 min, a następnie co godzinę, łącznie przez około 24 h.
- Wiadomości są w formacie tekstowym, kodowanie UTF-8. Nadawca i jego nazwa (fabrycznie „MonitorUp.pl”) są ustawiane na stronie sterownika.
- Każda wysyłka i każdy błąd wysyłki są zapisywane w dzienniku zdarzeń sterownika.

**Temat wiadomości:** `[MonitorUp.pl] <KATEGORIA> – <rodzaj zdarzenia> – <nazwa zbiornika>`

| Kategoria | Rodzaje wiadomości |
|---|---|
| ALARM | poziom krytyczny, przepełnienie, nagły spadek poziomu |
| OSTRZEŻENIE | awaria czujnika, poziom minimalny, usterka zbiorcza |
| INFORMACJA | powrót do stanu normalnego, skasowanie alarmu |
| RAPORT | raport dzienny |
| TEST | wiadomość testowa |

## 2. Budowa treści

Każda wiadomość ma tę samą budowę:

| Wiersz | Zawartość |
|---|---|
| Nagłówek | `MonitorUp.pl – <rodzaj zdarzenia>` |
| Urządzenie | nazwa urządzenia z ustawień |
| Czas zdarzenia | data i godzina wystąpienia zdarzenia (czas lokalny) |
| Zbiornik | nazwa zbiornika |
| Stan | bieżący stan wg panelu, np. „ALARM – poziom krytyczny” |
| Poziom | poziom [cm] i wypełnienie [%]; przy awarii czujnika – „brak pomiaru” i prąd czujnika [mA] |
| Objętość | objętość i pojemność [l] – gdy skonfigurowano przeliczanie na litry |
| Zużycie | tylko raport dzienny: dziś, wczoraj, średnio z 7 dni |
| Zalecane działanie | działanie zgodne z panelem – gdy dotyczy |
| Szczegóły | informacja dodatkowa właściwa dla rodzaju zdarzenia |
| Strona sterownika | adres `http://<IP>/` – gdy sterownik ma adres w sieci |
| Stopka | informacja o automatycznym wysłaniu i wersji oprogramowania |

**Przykład – poziom krytyczny:**

```
Temat: [MonitorUp.pl] ALARM – Poziom krytyczny – Zbiornik paliwa

MonitorUp.pl – Poziom krytyczny
Urządzenie: MonitorUp.pl
Czas zdarzenia: 18.11.2026 06:42:15

Zbiornik: Zbiornik paliwa
Stan: ALARM – poziom krytyczny
Poziom: 17,2 cm (8,6 %)
Objętość: 410 l z 5000 l

Zalecane działanie: uzupełnić zbiornik
Szczegóły: Poprzedni stan: poziom minimalny

Strona sterownika: http://192.168.0.12/

--
Wiadomość wysłana automatycznie przez sterownik MonitorUp.pl (SW 0.5.5).
Odbiorców i rodzaj powiadomień ustawia się na stronie sterownika:
Ustawienia → Powiadomienia.
```

## 3. Wykaz wiadomości

| Nr | Rodzaj (pole wyboru u odbiorcy) | Temat | Wysyłana, gdy | Zalecane działanie | Szczegóły |
|---|---|---|---|---|---|
| 1 | Poziom krytyczny | `[MonitorUp.pl] ALARM – Poziom krytyczny – <zbiornik>` | poziom spadnie do progu krytycznego (po opóźnieniu zadziałania) | uzupełnić zbiornik | Poprzedni stan: … |
| 2 | Przepełnienie | `[MonitorUp.pl] ALARM – Przepełnienie – <zbiornik>` | poziom przekroczy próg przepełnienia | wstrzymać napełnianie | Poprzedni stan: … |
| 3 | Nagły spadek poziomu | `[MonitorUp.pl] ALARM – Nagły spadek poziomu – <zbiornik>` | poziom spadnie o zadaną wartość w zadanym czasie (także czulszy próg nocny) | sprawdzić szczelność zbiornika i instalacji | Poprzedni stan: … |
| 4 | Awaria czujnika | `[MonitorUp.pl] OSTRZEŻENIE – Awaria czujnika – <zbiornik>` | prąd przetwornika poniżej 3,6 mA lub powyżej 21 mA | wezwać serwis | Poprzedni stan: … |
| 5 | Poziom minimalny | `[MonitorUp.pl] OSTRZEŻENIE – Poziom minimalny – <zbiornik>` | poziom spadnie do progu minimalnego | zaplanować uzupełnienie | Poprzedni stan: … |
| 6 | Powrót do stanu normalnego | `[MonitorUp.pl] INFORMACJA – Powrót do stanu normalnego – <zbiornik>` | ustąpi alarm lub ostrzeżenie | – | Poprzedni stan: … |
| 7 | Skasowanie alarmu | `[MonitorUp.pl] INFORMACJA – Skasowanie alarmu – <zbiornik>` | alarm zostanie potwierdzony | – | Źródło skasowania: przycisk KASOWANIE ALARMU / strona WWW sterownika / Modbus TCP / MQTT / API HTTP |
| 8 | Usterka zbiorcza | `[MonitorUp.pl] OSTRZEŻENIE – Usterka zbiorcza – <zbiornik>` | pojawi się nowa przyczyna usterki zbiorczej wybrana w ustawieniach wyjść | sprawdzić stan urządzenia na stronie sterownika | Przyczyna: przetwornik A/C, karta SD, czas nieustawiony, brak sieci, brak połączenia MQTT, ekspander wejść/wyjść |
| 9 | Raport dzienny | `[MonitorUp.pl] RAPORT – Raport dzienny – <zbiornik>` | codziennie o ustawionej godzinie (fabrycznie 7:00), gdy czas jest zsynchronizowany | – | zużycie dziś, wczoraj i średnie z 7 dni (w litrach lub cm) |
| 10 | – (przycisk „Wyślij test”) | `[MonitorUp.pl] TEST – Wiadomość testowa – <zbiornik>` | użytkownik wybierze „Wyślij test” na stronie sterownika | – | Konfiguracja serwera SMTP jest poprawna. |

## 4. Wartości „Poprzedni stan”

| Wartość | Znaczenie |
|---|---|
| stan normalny | przed zdarzeniem poziom był w zakresie normalnym |
| poziom minimalny | przed zdarzeniem trwało ostrzeżenie o poziomie minimalnym |
| poziom krytyczny | przed zdarzeniem trwał alarm poziomu krytycznego |
| przepełnienie | przed zdarzeniem trwał alarm przepełnienia |
| awaria czujnika | przed zdarzeniem pomiar był niedostępny |
| nagły spadek poziomu | przed zdarzeniem trwał alarm nagłego spadku |

## 5. Ustawienia fabryczne odbiorcy

| Rodzaj | Fabrycznie zaznaczony |
|---|---|
| Poziom krytyczny, przepełnienie, nagły spadek poziomu | tak |
| Awaria czujnika, poziom minimalny | tak |
| Powrót do stanu normalnego | tak |
| Skasowanie alarmu | nie |
| Usterka zbiorcza | nie |
| Raport dzienny | nie |

---

Producent: **[do uzupełnienia]** · www.monitorup.pl
