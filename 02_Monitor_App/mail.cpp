// Powiadomienia e-mail (0.5.6) - wlasny klient SMTP, bez bibliotek zewnetrznych.
// Szyfrowanie: NetworkClientSecure (SSL/TLS od razu albo STARTTLS po EHLO), certyfikat sprawdzany wbudowanym pakietem CA.
// Tresc i temat w UTF-8 (temat RFC 2047, tresc base64). Dziennik zdarzen piszemy tylko z petli glownej (kolejka logow).
#include "mail.h"
#include <NetworkClientSecure.h>
#include <ETH.h>
#include <WiFi.h>
#include <base64.h>
#include <time.h>
#include "config.h"
#include "sensors.h"
#include "outputs.h"
#include "stats.h"
#include "logger.h"
#include "hw.h"

struct MailMsg { uint16_t kind; uint8_t tank; char to[64]; char detail[120]; time_t t; time_t next; uint8_t tries; };
struct MailLog { char text[100]; };

static QueueHandle_t mailQ = nullptr, logQ = nullptr;
static volatile uint32_t lastSent[16];       // millis() ostatniej wiadomosci danego rodzaju (bit -> indeks)
static String statusText = "nie wysyłano";
static SemaphoreHandle_t stMux = nullptr;

static void setStatus(const String &s) {
  if (stMux) xSemaphoreTake(stMux, portMAX_DELAY);
  statusText = s;
  if (stMux) xSemaphoreGive(stMux);
}
String mailStatusText() {
  if (!cfg.mailEn) return "powiadomienia wyłączone";
  if (stMux) xSemaphoreTake(stMux, portMAX_DELAY);
  String s = statusText;
  if (stMux) xSemaphoreGive(stMux);
  return s;
}

static void logPost(const String &s) {
  if (!logQ) return;
  MailLog m; strlcpy(m.text, s.c_str(), sizeof(m.text));
  xQueueSend(logQ, &m, 0);
}

static int kindIdx(uint16_t k) { for (int i = 0; i < 16; i++) if (k & (1u << i)) return i; return 0; }

// ---------------- Teksty ----------------
static const char *kindLabel(uint16_t k) {
  switch (k) {
    case MK_CRIT: return "Poziom krytyczny";
    case MK_HIGH: return "Przepełnienie";
    case MK_DROP: return "Nagły spadek poziomu";
    case MK_SENSOR: return "Awaria czujnika";
    case MK_MIN: return "Poziom minimalny";
    case MK_NORMAL: return "Powrót do stanu normalnego";
    case MK_ACK: return "Skasowanie alarmu";
    case MK_SYSFAULT: return "Usterka zbiorcza";
    case MK_DAILY: return "Raport dzienny";
    case MK_TEST: return "Wiadomość testowa";
  }
  return "Zdarzenie";
}
static const char *kindClass(uint16_t k) {
  if (k & (MK_CRIT | MK_HIGH | MK_DROP)) return "ALARM";
  if (k & (MK_SENSOR | MK_MIN | MK_SYSFAULT)) return "OSTRZEŻENIE";
  if (k == MK_DAILY) return "RAPORT";
  if (k == MK_TEST) return "TEST";
  return "INFORMACJA";
}
static const char *kindAction(uint16_t k) {
  switch (k) {
    case MK_CRIT: return "uzupełnić zbiornik";
    case MK_HIGH: return "wstrzymać napełnianie";
    case MK_DROP: return "sprawdzić szczelność zbiornika i instalacji";
    case MK_SENSOR: return "wezwać serwis";
    case MK_MIN: return "zaplanować uzupełnienie";
    case MK_SYSFAULT: return "sprawdzić stan urządzenia na stronie sterownika";
  }
  return "";
}
static const char *panelState(uint8_t s) {
  switch (s) {
    case ST_NORMAL: return "NORMA – stan normalny";
    case ST_MIN: return "OSTRZEŻENIE – poziom minimalny";
    case ST_FAULT: return "OSTRZEŻENIE – awaria czujnika";
    case ST_CRIT: return "ALARM – poziom krytyczny";
    case ST_HIGH: return "ALARM – przepełnienie";
    case ST_DROP: return "ALARM – nagły spadek poziomu";
    case ST_OFF: return "zbiornik nieaktywny";
  }
  return "?";
}
static String pl(float v, uint8_t d) { if (isnan(v)) return "–"; String s = String(v, (unsigned)d); s.replace('.', ','); return s; }
static String fmtTime(time_t t, const char *f) { struct tm tm; localtime_r(&t, &tm); char b[48]; strftime(b, sizeof(b), f, &tm); return b; }
static String devIp() {
  if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) return ETH.localIP().toString();
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return "";
}

static String tankBlock(uint8_t i) {
  TankState s = tankGet(i);
  bool L = cfg.tx[i].volEn;
  String b = "Zbiornik: " + String(cfg.tankName[i]) + "\r\n";
  b += "Stan: " + String(panelState(tankEffState(i))) + "\r\n";
  if (s.valid) {
    b += "Poziom: " + pl(s.cm, 1) + " cm (" + pl(s.pct, 1) + " %)\r\n";
    if (L && s.vol >= 0) b += "Objętość: " + pl(s.vol, 0) + " l z " + pl(s.volTotal, 0) + " l\r\n";
  } else b += "Poziom: brak pomiaru (prąd czujnika " + pl(s.mA, 2) + " mA)\r\n";
  return b;
}

static void compose(const MailMsg &m, String &subj, String &body) {
  uint8_t i = m.tank < TANK_COUNT ? m.tank : 0;
  subj = String("[MonitorUp.pl] ") + kindClass(m.kind) + " – " + kindLabel(m.kind) + " – " + cfg.tankName[i];
  body = String("MonitorUp.pl – ") + kindLabel(m.kind) + "\r\n";
  body += "Urządzenie: " + String(cfg.devName) + "\r\n";
  body += "Czas zdarzenia: " + fmtTime(m.t, "%d.%m.%Y %H:%M:%S") + "\r\n\r\n";
  body += tankBlock(i);
  if (m.kind == MK_DAILY) {
    bool L = cfg.tx[i].volEn;
    const char *u = L ? " l" : " cm";
    body += "\r\nZużycie dziś (do godz. " + fmtTime(time(nullptr), "%H:%M") + "): " + pl(statConsToday(i, L), L ? 0 : 1) + u + "\r\n";
    body += "Zużycie wczoraj: " + pl(statYesterday(i, L), L ? 0 : 1) + u + "\r\n";
    body += "Średnie zużycie dobowe (7 dni): " + pl(statAvg(i, 7, L), L ? 0 : 1) + u + "\r\n";
  }
  const char *act = kindAction(m.kind);
  if (*act) body += String("\r\nZalecane działanie: ") + act + "\r\n";
  if (m.detail[0]) body += String("Szczegóły: ") + m.detail + "\r\n";
  String ip = devIp();
  if (ip.length()) body += "\r\nStrona sterownika: http://" + ip + "/\r\n";
  body += "\r\n--\r\nWiadomość wysłana automatycznie przez sterownik MonitorUp.pl (SW " FW_VERSION ").\r\n"
          "Odbiorców i rodzaj powiadomień ustawia się na stronie sterownika: Ustawienia → Powiadomienia.\r\n";
}

// ---------------- SMTP ----------------
static NetworkClientSecure cli;

static int smtpRead(String &last, uint32_t tmo = 15000) {
  uint32_t t0 = millis();
  String line;
  while (millis() - t0 < tmo) {
    while (cli.available()) {
      char c = cli.read();
      if (c == '\n') {
        line.trim();
        last = line;
        if (line.length() >= 4 && line[3] == '-') { line = ""; continue; }   // odpowiedz wielowierszowa
        return line.length() >= 3 ? line.substring(0, 3).toInt() : -1;
      }
      line += c;
    }
    if (!cli.connected()) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  last = "brak odpowiedzi serwera";
  return -1;
}
static int smtpCmd(const String &cmd, String &last) { cli.print(cmd + "\r\n"); return smtpRead(last); }

static String b64(const String &s) { return base64::encode(s); }
static String encHdr(const String &s) { return "=?UTF-8?B?" + b64(s) + "?="; }

// true = wyslano; err = opis bledu
static bool smtpSend(const std::vector<String> &to, const String &subj, const String &body, String &err) {
  String r;
  cli.stop();
  cli.setTimeout(15);
  if (cfg.smtpVerify) cli.useBuiltinCACertBundle(); else cli.setInsecure();
  if (cfg.smtpSec != 1) cli.setPlainStart();
  if (!cli.connect(cfg.smtpHost, cfg.smtpPort)) { err = String("brak połączenia z ") + cfg.smtpHost + ":" + cfg.smtpPort; return false; }
  auto fail = [&](const char *step) { err = String(step) + ": " + r; cli.stop(); return false; };
  if (smtpRead(r) != 220) return fail("powitanie");
  String me = "monitorup";
  if (smtpCmd("EHLO " + me, r) != 250) return fail("EHLO");
  if (cfg.smtpSec == 2) {
    if (smtpCmd("STARTTLS", r) != 220) return fail("STARTTLS");
    if (cli.startTLS() != 1) { err = "STARTTLS: błąd negocjacji TLS (certyfikat?)"; cli.stop(); return false; }
    if (smtpCmd("EHLO " + me, r) != 250) return fail("EHLO po STARTTLS");
  }
  if (cfg.smtpUser[0]) {
    if (smtpCmd("AUTH LOGIN", r) != 334) return fail("AUTH");
    if (smtpCmd(b64(cfg.smtpUser), r) != 334) return fail("AUTH login");
    if (smtpCmd(b64(cfg.smtpPass), r) != 235) return fail("logowanie (hasło?)");
  }
  if (smtpCmd(String("MAIL FROM:<") + cfg.mailFrom + ">", r) != 250) return fail("nadawca");
  uint8_t ok = 0;
  for (auto &a : to) if (smtpCmd("RCPT TO:<" + a + ">", r) / 10 == 25) ok++;   // 250 / 251
  if (!ok) return fail("odbiorca");
  if (smtpCmd("DATA", r) != 354) return fail("DATA");
  String toHdr;
  for (auto &a : to) { if (toHdr.length()) toHdr += ", "; toHdr += a; }
  time_t now = time(nullptr);
  String h = "From: " + encHdr(cfg.mailFromName) + " <" + cfg.mailFrom + ">\r\n";
  h += "To: " + toHdr + "\r\n";
  h += "Subject: " + encHdr(subj) + "\r\n";
  h += "Date: " + fmtTime(now, "%a, %d %b %Y %H:%M:%S %z") + "\r\n";
  h += "Message-ID: <" + String((uint32_t)now) + "." + String(esp_random(), HEX) + "@monitorup.pl>\r\n";
  h += "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n";
  h += "X-Mailer: MonitorUp.pl " FW_VERSION "\r\n\r\n";
  cli.print(h);
  String e = b64(body);
  for (size_t p = 0; p < e.length(); p += 76) { cli.print(e.substring(p, p + 76)); cli.print("\r\n"); }
  if (smtpCmd(".", r) != 250) return fail("wysłanie treści");
  smtpCmd("QUIT", r);
  cli.stop();
  return true;
}

// ---------------- Zadanie wysylki ----------------
// Jedna proba wysylki; false = do ponowienia
static bool deliver(MailMsg &m) {
  if (!cfg.mailEn && m.kind != MK_TEST) return true;
  std::vector<String> to;
  if (m.kind == MK_TEST) to.push_back(m.to);
  else for (auto &x : cfg.rcpt) if (x.addr[0] && (x.mask & m.kind)) to.push_back(x.addr);
  if (to.empty()) return true;
  String subj, body, err;
  compose(m, subj, body);
  uint32_t t0 = millis();
  if (smtpSend(to, subj, body, err)) {
    setStatus(String("wysłano ") + fmtTime(time(nullptr), "%d.%m %H:%M") + " – " + kindLabel(m.kind) + " (" + to.size() + " odb., " + (millis() - t0) / 1000 + " s)");
    logPost(String("Wysłano e-mail: ") + kindLabel(m.kind) + " → " + to[0] + (to.size() > 1 ? " i inni" : ""));
    return true;
  }
  setStatus("błąd " + fmtTime(time(nullptr), "%d.%m %H:%M") + " – " + err);
  logPost("Błąd e-mail (" + String(kindLabel(m.kind)) + "): " + err.substring(0, 60));
  return m.kind == MK_TEST;   // test nie jest ponawiany - wynik widac od razu na stronie
}

static void mailTask(void *) {
  static const uint16_t waitMin[] = {1, 5, 15, 60};   // potem co 60 min, lacznie ok. 24 h
  std::vector<MailMsg> retry;
  MailMsg m;
  for (;;) {
    if (xQueueReceive(mailQ, &m, pdMS_TO_TICKS(20000)) == pdTRUE && !deliver(m) && retry.size() < 10) {
      m.next = time(nullptr) + waitMin[0] * 60; retry.push_back(m);
    }
    time_t now = time(nullptr);
    for (size_t k = 0; k < retry.size();) {
      MailMsg &r = retry[k];
      if (now < r.next) { k++; continue; }
      if (deliver(r) || ++r.tries > 27) { retry.erase(retry.begin() + k); continue; }
      r.next = now + waitMin[min<uint8_t>(r.tries - 1, 3)] * 60;
      k++;
    }
  }
}

void mailBegin() {
  stMux = xSemaphoreCreateMutex();
  mailQ = xQueueCreate(8, sizeof(MailMsg));
  logQ = xQueueCreate(8, sizeof(MailLog));
  xTaskCreatePinnedToCore(mailTask, "mail", 12288, nullptr, 1, nullptr, 0);
}

static bool enqueue(uint16_t kind, uint8_t tank, const String &detail, const char *to = "") {
  if (!mailQ) return false;
  MailMsg m = {};
  m.kind = kind; m.tank = tank; m.t = time(nullptr); m.tries = 1;
  strlcpy(m.to, to, sizeof(m.to)); strlcpy(m.detail, detail.c_str(), sizeof(m.detail));
  return xQueueSend(mailQ, &m, 0) == pdTRUE;
}

void mailEvent(uint16_t kind, uint8_t tank, const String &detail) {
  if (!cfg.mailEn) return;
  bool wanted = false;
  for (auto &x : cfg.rcpt) wanted |= x.addr[0] && (x.mask & kind);
  if (!wanted) return;
  int k = kindIdx(kind);
  uint32_t now = millis();
  if (lastSent[k] && cfg.mailGapMin && now - lastSent[k] < (uint32_t)cfg.mailGapMin * 60000UL) return;   // ochrona przed zalewem
  lastSent[k] = now ? now : 1;
  if (!enqueue(kind, tank, detail)) logEvent("E-MAIL", "Kolejka powiadomień pełna – pominięto: " + String(kindLabel(kind)));
}

bool mailTest(const String &to) {
  if (!strchr(to.c_str(), '@') || !cfg.smtpHost[0]) return false;
  return enqueue(MK_TEST, 0, "Konfiguracja serwera SMTP jest poprawna.", to.c_str());
}

void mailLoop() {
  MailLog l;
  while (logQ && xQueueReceive(logQ, &l, 0) == pdTRUE) logEvent("E-MAIL", l.text);
  // raport dzienny o pelnej godzinie (czas musi byc ustawiony)
  static int lastDay = -1;
  time_t t = time(nullptr);
  if (!cfg.mailEn || t < 1700000000) return;
  struct tm tm; localtime_r(&t, &tm);
  if (tm.tm_hour == cfg.mailDailyHour && tm.tm_min == 0 && tm.tm_yday != lastDay) {
    lastDay = tm.tm_yday;
    for (uint8_t i = 0; i < TANK_COUNT; i++)
      if (cfg.tank[i].enabled) {
        bool wanted = false;
        for (auto &x : cfg.rcpt) wanted |= x.addr[0] && (x.mask & MK_DAILY);
        if (wanted) enqueue(MK_DAILY, i, "");
      }
  }
}
