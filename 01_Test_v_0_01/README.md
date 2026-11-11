# 01_Test_v_0_01 – program testowy KinCony B8M

Firmware (Arduino, rdzeń ESP32 3.3.2) do sprawdzenia wszystkich peryferiów płytki KinCony B8M.

## Dostęp

| | |
|---|---|
| WiFi AP | SSID `KinCony-B8M-Test`, hasło `b8mtest123` |
| Strona | http://192.168.4.1 lub http://b8m-test.local |
| Ethernet | DHCP, ta sama strona pod adresem IP z routera (widoczny na OLED i w logu) |
| Log | port USB (COM8), 115200 bps |

## Co testuje

| Peryferium | Test na stronie |
|---|---|
| 8 × MOSFET (PCF8575 @0x22, P0–P7) | przełączanie pojedynczo, wszystkie ON/OFF, test kolejny (chase). Wymaga zasilania 12/24 V |
| 8 × DI (PCF8575 P8–P15) | podgląd na żywo |
| Przycisk Download (GPIO0) | podgląd |
| ADS1115 @0x48, A1–A4 | napięcie i wartość surowa (zakres ±6,144 V) |
| DS3231 @0x68 | czas, temperatura, flaga OSF, ustawienie czasu z przeglądarki |
| EEPROM 24C02 @0x50 | zapis/odczyt bajtu 0xFF z przywróceniem wartości |
| OLED SSD1306 @0x3C | ekran statusu, wszystkie piksele, inwersja, wygaszenie |
| Karta SD (SPI) | wykrycie (GPIO5), montowanie, zapis/odczyt pliku `/b8m_test.txt` |
| 1-Wire GPIO47/48/7/40 | wykrywanie DS18B20 i temperatura (rezystory podciągające są na płytce wg schematu KinCony, czujnik: VDD→3,3 V, GND→GND, DQ→GPIO) |
| GPIO13/14/21 | wejście z podciąganiem albo wyjście H/L |
| RS485 GPIO38/39 | wysyłanie tekstu, podgląd odebranych danych (ASCII/HEX) |
| UART Tuya GPIO17/16 | wysyłanie HEX, podgląd odebranych bajtów |
| Ethernet W5500 | stan łącza, prędkość, IP, MAC |
| I2C | skan magistrali |

Po starcie wykonywany jest autotest, którego wynik widać na stronie i w logu szeregowym.

## Kompilacja i wgrywanie (arduino-cli z Arduino IDE)

FQBN:

```
esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB
```

W Arduino IDE: płytka **ESP32S3 Dev Module**, USB CDC On Boot: Enabled, Flash Size: 16MB, PSRAM: OPI PSRAM,
Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS).

Wymagane biblioteki (są już zainstalowane): OneWire, DallasTemperature. OLED obsługuje własny sterownik w `oled.h`.

Jeśli wgrywanie się nie udaje, przytrzymaj przycisk **Download** i naciśnij **Reset**.
