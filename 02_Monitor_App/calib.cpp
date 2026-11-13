// MonitorUp - tabela kalibracji pradu wejscia A4 (od 0.4.8), opis w calib.h
#include "calib.h"
#include <Preferences.h>
#include "hw.h"

CalTable calTab;
String calSource = "brak";

#define CAL_MAGIC   0x5443u        // "CT"
#define CAL_VER     1
#define EE_CAL_BASE 128            // 0..105 zajmuje kopia kalibracji/progow (config.cpp)
// [0..1] 'C''T', [2] wersja, [3] n, [4..] raw uint16 x n, mA*1000 (uA) uint16 x n, CRC16
#define EE_CAL_LEN(n) (4 + 4 * (n) + 2)
static_assert(EE_CAL_BASE + EE_CAL_LEN(CAL_MAX) <= 256, "tabela kalibracji nie miesci sie w 24C02");

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}

static bool valid(const CalTable &t) {
  if (t.magic != CAL_MAGIC || t.n < 2 || t.n > CAL_MAX) return false;
  for (uint8_t i = 1; i < t.n; i++)
    if (!(t.raw[i] > t.raw[i - 1]) || !(t.mA[i] > t.mA[i - 1])) return false;
  return true;
}

static bool eeSave(const CalTable &t) {
  uint8_t buf[EE_CAL_LEN(CAL_MAX)];
  uint8_t n = t.n;
  buf[0] = 'C'; buf[1] = 'T'; buf[2] = CAL_VER; buf[3] = n;
  for (uint8_t i = 0; i < n; i++) {
    uint16_t r = (uint16_t)constrain(lroundf(t.raw[i]), 0L, 65535L);
    uint16_t u = (uint16_t)constrain(lroundf(t.mA[i] * 1000.0f), 0L, 65535L);
    buf[4 + 2 * i] = r & 0xFF;           buf[5 + 2 * i] = r >> 8;
    buf[4 + 2 * n + 2 * i] = u & 0xFF;   buf[5 + 2 * n + 2 * i] = u >> 8;
  }
  uint16_t c = crc16(buf, 4 + 4 * n);
  buf[4 + 4 * n] = c & 0xFF; buf[5 + 4 * n] = c >> 8;
  return eeWriteBlock(EE_CAL_BASE, buf, EE_CAL_LEN(n));
}

static bool eeLoad(CalTable &t) {
  uint8_t hdr[4];
  if (!eeReadBlock(EE_CAL_BASE, hdr, 4)) return false;
  if (hdr[0] != 'C' || hdr[1] != 'T' || hdr[2] != CAL_VER || hdr[3] < 2 || hdr[3] > CAL_MAX) return false;
  uint8_t n = hdr[3];
  uint8_t buf[EE_CAL_LEN(CAL_MAX)];
  if (!eeReadBlock(EE_CAL_BASE, buf, EE_CAL_LEN(n))) return false;
  if ((buf[4 + 4 * n] | (buf[5 + 4 * n] << 8)) != crc16(buf, 4 + 4 * n)) return false;
  memset(&t, 0, sizeof(t));
  t.magic = CAL_MAGIC; t.ver = CAL_VER; t.n = n;
  for (uint8_t i = 0; i < n; i++) {
    t.raw[i] = buf[4 + 2 * i] | (buf[5 + 2 * i] << 8);
    t.mA[i] = (buf[4 + 2 * n + 2 * i] | (buf[5 + 2 * n + 2 * i] << 8)) / 1000.0f;
  }
  strlcpy(t.date, "?", sizeof(t.date));
  strlcpy(t.ref, "odtworzona z kopii EEPROM", sizeof(t.ref));
  return valid(t);
}

static void nvsSave(const CalTable &t) {
  Preferences p;
  p.begin("monitor", false);
  if (t.n) p.putBytes("mAcal", &t, sizeof(t)); else p.remove("mAcal");
  p.end();
}

void calLoad() {
  memset(&calTab, 0, sizeof(calTab));
  Preferences p;
  p.begin("monitor", true);
  bool ok = p.getBytesLength("mAcal") == sizeof(calTab) && p.getBytes("mAcal", &calTab, sizeof(calTab)) == sizeof(calTab) && valid(calTab);
  p.end();
  if (ok) {
    calSource = "NVS";
    uint8_t h[4];                                   // brak kopii w EEPROM (np. wymieniona plytka) - dopisz
    if (eeReadBlock(EE_CAL_BASE, h, 4) && !(h[0] == 'C' && h[1] == 'T' && h[3] == calTab.n)) eeSave(calTab);
    return;
  }
  CalTable e;
  if (eeLoad(e)) {
    calTab = e;
    nvsSave(calTab);
    calSource = "EEPROM (odtworzono w NVS)";
    return;
  }
  memset(&calTab, 0, sizeof(calTab));
  calSource = "brak";
}

float calRawToMa(float raw) {
  const CalTable &t = calTab;
  if (t.n < 2) return raw * ADS_FS_V / 32768.0f * 1000.0f / SHUNT_OHM;
  uint8_t k = 1;                                    // odcinek; poza zakresem - przedluzenie skrajnego
  while (k < t.n - 1 && raw > t.raw[k]) k++;
  float x0 = t.raw[k - 1], x1 = t.raw[k], y0 = t.mA[k - 1], y1 = t.mA[k];
  return y0 + (y1 - y0) * (raw - x0) / (x1 - x0);
}

String calFromJson(JsonObjectConst f) {
  const char *typ = f["typ"] | "";
  if (strcmp(typ, "monitorup-kalibracja-mA")) return "To nie jest plik kalibracji MonitorUp (typ: monitorup-kalibracja-mA)";
  if ((f["wersja"] | 0) != 1) return "Nieobsługiwana wersja pliku kalibracji";
  const char *kanal = f["kanal"] | "A4";
  if (strcmp(kanal, "A4")) return "Plik dotyczy innego wejścia niż A4";
  JsonArrayConst pk = f["punkty"].as<JsonArrayConst>();
  if (pk.isNull() || pk.size() < 2) return "Plik musi mieć co najmniej 2 punkty";
  if (pk.size() > CAL_MAX) return "Za dużo punktów (maks. " + String(CAL_MAX) + ")";
  CalTable t;
  memset(&t, 0, sizeof(t));
  t.magic = CAL_MAGIC; t.ver = CAL_VER;
  for (JsonArrayConst p : pk) {
    float r = p[0] | -1.0f, m = p[1] | -1.0f;
    if (r < 0 || r > 32767 || m <= 0 || m > 30) return "Punkt " + String(t.n + 1) + ": wartość poza zakresem";
    t.raw[t.n] = r; t.mA[t.n] = m; t.n++;
  }
  if (!valid(t)) return "Punkty muszą rosnąć w obu kolumnach (surowy odczyt i mA)";
  // kontrola sensownosci: odchylka od wzoru 150 ohm maks. 1,5 mA w zakresie 4-20 mA
  for (uint8_t i = 0; i < t.n; i++) {
    float nom = t.raw[i] * ADS_FS_V / 32768.0f * 1000.0f / SHUNT_OHM;
    if (t.mA[i] >= 3.5f && t.mA[i] <= 21.0f && fabsf(t.mA[i] - nom) > 1.5f)
      return "Punkt " + String(i + 1) + " odbiega od wzoru o " + String(t.mA[i] - nom, 2) + " mA – plik z innego urządzenia?";
  }
  strlcpy(t.date, f["data"] | "", sizeof(t.date));
  strlcpy(t.ref, f["wzorzec"] | "", sizeof(t.ref));
  calTab = t;
  nvsSave(calTab);
  if (!eeSave(calTab)) return "Zapisano w pamięci, ale nie udało się zapisać kopii w EEPROM";
  calSource = "NVS";
  return "";
}

void calToJson(JsonObject o) {
  o["typ"] = "monitorup-kalibracja-mA"; o["wersja"] = 1; o["kanal"] = "A4";
  o["data"] = calTab.date; o["wzorzec"] = calTab.ref; o["zrodlo"] = calSource;
  o["rezystor"] = SHUNT_OHM;
  JsonArray a = o["punkty"].to<JsonArray>();
  for (uint8_t i = 0; i < calTab.n; i++) { JsonArray p = a.add<JsonArray>(); p.add(calTab.raw[i]); p.add(calTab.mA[i]); }
}

void calClear() {
  memset(&calTab, 0, sizeof(calTab));
  nvsSave(calTab);
  uint8_t z[4] = {0, 0, 0, 0};                      // unieważnij kopie w EEPROM (naglowek)
  eeWriteBlock(EE_CAL_BASE, z, 4);
  calSource = "brak";
}
