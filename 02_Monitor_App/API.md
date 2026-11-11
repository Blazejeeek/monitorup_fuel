# MonitorUp – API HTTP (`/api/v1`)

Wszystkie odpowiedzi w JSON (UTF-8). Błędy: `{"error": "opis"}` z kodem 400 / 401 / 403 / 404 / 429 / 503.

## Uwierzytelnianie

- **Strona WWW:** sesja w ciasteczku `MA_SID` po `POST /api/v1/login`.
- **Integracje:** token API w nagłówku `Authorization: Bearer ma_…` (Ustawienia → API). Token jest pokazywany tylko raz, urządzenie trzyma jego skrót SHA-256.
- Poziomy tokenów: **odczyt** (0), **odczyt + sterowanie** (1), **konfiguracja** (2). Hasło, tokeny, kopia ustawień, OTA i restart wymagają zalogowania na stronie.
- Limit: 10 zapytań/s na token; 5 błędnych tokenów z jednego IP = blokada IP na 5 min.
- Opcja „odczyt bez tokenu z sieci lokalnej” (domyślnie wyłączona).

```
curl -H "Authorization: Bearer ma_xxxxxxxx" http://192.168.0.12/api/v1/status
```

## Odczyt (token: odczyt)

| Metoda | Adres | Opis |
|---|---|---|
| GET | `/api/v1/status` | poziomy, stany, litry, statystyki, alarm, wyjścia, wejścia, sieć, czas, SD |
| GET | `/api/v1/history` | ostatnie 24 h co 5 min (poziom cm), do wykresu |
| GET | `/api/v1/logs?from=RRRR-MM-DD&to=RRRR-MM-DD&max=1500` | `rows: [[epoch, z1_cm, z2_cm, z1_l, z2_l], …]` (z redukcją punktów) |
| GET | `/api/v1/logs/csv?from=…&to=…` | plik CSV (separator `;`, przecinek dziesiętny) |
| GET | `/api/v1/logs/files` | lista plików dziennych na karcie |
| GET | `/api/v1/stats` | zużycie dzienne (do 30 dni), dziś |
| GET | `/api/v1/events[?m=RRRR-MM]` | dziennik zdarzeń (bez `m`: ostatnie 60 z pamięci) |
| GET | `/api/v1/modbus/map.csv` | mapa rejestrów Modbus |

Kody stanu zbiornika (`state`): 0 NORMALNY, 1 MINIMALNY, 2 KRYTYCZNY, 3 ZA DUŻY, 4 BŁĄD CZUJNIKA, 5 WYŁĄCZONY, 6 NAGŁY SPADEK.

## Sterowanie (token: odczyt + sterowanie)

| Metoda | Adres | Treść |
|---|---|---|
| POST | `/api/v1/alarm/ack` | `{}` – kasowanie alarmu |
| POST | `/api/v1/alarm/suspend` | `{"tank":1,"minutes":120}` – wstrzymanie alarmu nagłego spadku |
| POST | `/api/v1/outputs` | `{"n":4,"value":60}` – wyjście w trybie „ręczne” (0–100 %); `{"test":true}` – test wyjść |

## Konfiguracja (token: konfiguracja – sekcje `tanks`, `alarm`, `logs`)

| Metoda | Adres | Treść |
|---|---|---|
| POST | `/api/v1/config` | zapis częściowy, np. `{"tanks":[{"thrMin":70},{}]}` |

## Tylko po zalogowaniu na stronie

`GET /api/v1/config`, `GET /api/v1/wifi/scan[?start=1]`, `POST /api/v1/calibrate {"tank":0,"point":"min","cm":0}`,
`POST /api/v1/time {"epoch":…}`, `POST /api/v1/time/sync`, `GET/POST /api/v1/tokens`, `POST /api/v1/tokens/revoke {"id":0[,"renew":true]}`,
`GET /api/v1/backup` (bez haseł), `POST /api/v1/backup` `{"pass":"…"}` (z hasłami, zaszyfrowana AES-256-GCM; wgranie wymaga pola `_pass`), `POST /api/v1/restore`, `POST /api/v1/ota` (multipart, plik .bin), `POST /api/v1/reboot`,
`POST /api/v1/factory {"keepCal":true}`, `POST /api/v1/logs/clear`, `POST /api/v1/password`.

Połączenie HTTP nie jest szyfrowane. Do dostępu spoza sieci lokalnej używaj VPN.
