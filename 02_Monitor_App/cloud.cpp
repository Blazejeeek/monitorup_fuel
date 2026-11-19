// MonitorUp - polaczenie z chmura Zdalio (0.6.0, karta #179), opis w cloud.h
#include "cloud.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Update.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <freertos/semphr.h>
#include <vector>
#include "cloudproto.h"
#include "cloud_ca.h"
#include "config.h"
#include "cfgjson.h"
#include "sensors.h"
#include "stats.h"
#include "outputs.h"
#include "net.h"
#include "logger.h"
#include "hw.h"
#include "fab.h"
#include "timesync.h"
#include "otasig.h"
#include "web.h"

// Znacznik wersji w obrazie programu - sterownik porownuje go z numerem z polecenia OTA (cloudproto.h, FwTagScanner)
extern "C" __attribute__((used)) const char MU_FW_TAG[] = "MUFWTAG1:" FW_NUM_STR ":" FW_VERSION ";";

// Rollback bootloadera (CONFIG_APP_ROLLBACK_ENABLE w rdzeniu): nowy program po OTA potwierdzamy sami (cloudBootCheck)
extern "C" bool verifyRollbackLater() { return true; }

#define MODULE "e-tank"
static const uint32_t RETRY_MIN_MS = 15000, RETRY_MAX_MS = 300000, RETRY_TLS_MS = 300000, RETRY_AUTH_MS = 1800000;
static const uint32_t OTA_CONFIRM_MS = 600000;            // nowa wersja z chmury musi sie polaczyc w 10 min
static const uint32_t OTA_TOTAL_MS = 600000, OTA_IDLE_MS = 30000;

static WiFiClientSecure net;
static PubSubClient mq(net);
static SemaphoreHandle_t mux = nullptr;
struct Lock { Lock() { xSemaphoreTakeRecursive(mux, portMAX_DELAY); } ~Lock() { xSemaphoreGiveRecursive(mux); } };

static volatile uint8_t cst = CS_OFF;
static volatile bool applyReq = false;
static uint32_t nextTry = 0, retryMs = RETRY_MIN_MS, fails = 0, downSinceMs = 0, lastTele = 0;
static time_t sinceT = 0;
static uint32_t sentCnt = 0;
static volatile int otaPct = -1;
static volatile bool infoReq = false;
static char hwId[HW_ID_LEN + 1], base[40], bootId[9];
static EvtBuffer *buf = nullptr;
static CidCache cids;
static int64_t rejectedRev = -1;
static volatile uint32_t testUntil = 0;                   // PROV-CLOUDTEST: chmura wlaczona tylko w RAM (test na stanowisku)
static bool cloudOn() { return cfg.cloudEn || (testUntil && (int32_t)(testUntil - millis()) > 0); }

struct Out { std::string sfx, body; bool retain; };
static std::deque<Out> outbox;                            // publikacje zlecone z callbacku i petli glownej

enum MrKind : uint8_t { MR_LOG, MR_ACK, MR_REBOOT, MR_CFG, MR_SYSLOG };
struct MainReq { MrKind kind; std::string a, cid; int32_t n; };
static std::deque<MainReq> mainQ;                         // zadania dla petli glownej

struct LastCmd { uint32_t t; char name[14]; char res[12]; };
static LastCmd lastCmds[10];
static uint8_t lcHead = 0, lcCount = 0;

// rotacja hasla (cmd/sys/rotate)
static bool rotating = false;
static char rotPass[33], rotCid[CidCache::MAX_CID + 1];

// OTA z chmury
struct OtaReq { bool on; std::string url, sha, fw, cid; uint32_t size, num; bool down; };
static OtaReq ota;
// Zapis w NVS "cloud"/"try" na czas proby nowej wersji (przezywa restart)
struct TryRec { uint8_t active, down; char part[17], from[24], to[24], cid[41]; uint16_t fromNum, toNum; };
static TryRec tryRec;
static bool pendingVerify = false, rolledBack = false, tryAnnounced = false;

// ---------------- pomocnicze ----------------
static void mainPost(MrKind k, const std::string &a = "", const std::string &cid = "", int32_t n = 0) {
  Lock l; if (mainQ.size() < 24) mainQ.push_back(MainReq{k, a, cid, n});
}
static void logPost(const String &s) { mainPost(MR_LOG, s.c_str()); }
static void outPost(const char *sfx, const std::string &body, bool retain = false) {
  Lock l; if (outbox.size() < 24) outbox.push_back(Out{sfx, body, retain});
}

static bool idOk() { return fabProgrammed() && fab.cloudHost[0] && fab.cloudPort && hwId[0]; }

static uint64_t upMs() { return (uint64_t)(esp_timer_get_time() / 1000); }

static void envelope(JsonDocument &d, int64_t seq) {
  d["v"] = 1; d["boot"] = bootId;
  if (seq >= 0) d["seq"] = seq;
  if (timeOk()) {
    char t[24]; time_t n = time(nullptr); struct tm g; gmtime_r(&n, &g);
    strftime(t, sizeof(t), "%Y-%m-%dT%H:%M:%SZ", &g);
    d["ts"] = t; d["ts_ok"] = true;
  } else { d["ts"] = nullptr; d["ts_ok"] = false; }
  d["up_ms"] = upMs();
}

// Zdarzenie do bufora (dowolne zadanie). Za duze tresci skracane do limitu protokolu.
static void evtPush(const char *name, JsonDocument &data) {
  if (!cloudOn() || !idOk() || !buf) return;
  Lock l;
  uint32_t seq = buf->allocSeq();
  JsonDocument d;
  envelope(d, seq);
  d["data"] = data;
  size_t n = measureJson(d);
  if (n > CLOUD_MAX_PAYLOAD - 24 && d["data"]["msg"].is<const char *>()) {   // za dlugi opis - skracamy tylko msg
    String m = d["data"]["msg"].as<String>();
    size_t cut = n - (CLOUD_MAX_PAYLOAD - 24);
    d["data"]["msg"] = m.substring(0, m.length() > cut ? m.length() - cut : 0);
  }
  PStr body; body.resize(measureJson(d));
  serializeJson(d, &body[0], body.size() + 1);
  buf->push(seq, name, std::move(body));
}

static std::string resBody(const std::string &cid, const char *s, const char *code, JsonDocument *data) {
  JsonDocument d;
  d["cid"] = cid; d["st"] = s;
  if (code && *code) d["code"] = code;
  if (data) d["data"] = *data; else d["data"].to<JsonObject>();
  std::string o; serializeJson(d, o);
  return o;
}

static void noteCmd(const std::string &name, const char *res) {
  Lock l;
  LastCmd &c = lastCmds[lcHead];
  c.t = (uint32_t)time(nullptr);
  strlcpy(c.name, name.c_str(), sizeof(c.name)); strlcpy(c.res, res, sizeof(c.res));
  lcHead = (lcHead + 1) % 10; if (lcCount < 10) lcCount++;
}

// Wynik polecenia: do kolejki publikacji, pamieci cid, listy na stronie i dziennika
static void result(const std::string &name, const std::string &cid, const char *s, const char *code = "", JsonDocument *data = nullptr) {
  std::string b = resBody(cid, s, code, data);
  { Lock l; cids.put(cid, b); }
  outPost("res", b);
  noteCmd(name, s);
  logPost(String("Chmura: ") + name.c_str() + ", cid " + cid.c_str() + ", wynik " + s + (code && *code ? String(" (") + code + ")" : String("")));
}

// ---------------- stan ----------------
bool cloudConnected() { return cst == CS_OK || cst == CS_NOACK; }
uint8_t cloudState() { return cst; }
int cloudOtaPct() { return otaPct; }
bool cloudOtaBusy() { return cst == CS_OTA; }
String cloudHwId() { return hwId; }
String cloudHwLabel() { return hwLabel(hwId).c_str(); }

const char *cloudStCode() {
  static const char *C[] = {"off", "noid", "nonet", "conn", "ok", "noack", "ota", "dns", "tcp", "tls", "auth"};
  return cst <= CS_AUTH ? C[cst] : "?";
}

String cloudStatusText() {
  switch (cst) {
    case CS_OFF: return "Wyłączone";
    case CS_NOID: return "Brak danych chmury (sterownik niezaprogramowany)";
    case CS_NONET: return "Brak sieci";
    case CS_CONN: return fails ? String("Łączenie… (prób: ") + fails + ")" : String("Łączenie…");
    case CS_OK: case CS_NOACK: {
      String s = "Połączono";
      if (sinceT > 100000) { char b[24]; fmtTime(b, sizeof(b), "%Y-%m-%d %H:%M", sinceT); s += String(" od ") + b; }
      if (cst == CS_NOACK) s += ", chmura nie potwierdza danych";
      return s;
    }
    case CS_OTA: return String("Aktualizacja z chmury: ") + (otaPct < 0 ? 0 : otaPct) + " %";
    case CS_DNS: return "Nie znaleziono serwera (DNS)";
    case CS_TCP: return String("Serwer niedostępny – sprawdź, czy sieć pozwala na połączenie wychodzące na port ") + fab.cloudPort;
    case CS_TLS: return "Błąd certyfikatu serwera";
    case CS_AUTH: return "Logowanie odrzucone (konto zablokowane albo nieznane)";
  }
  return "?";
}

String cloudShortText() {
  switch (cst) {
    case CS_OFF: return "wyl.";
    case CS_NOID: return "brak danych";
    case CS_NONET: return "brak sieci";
    case CS_CONN: return "laczenie";
    case CS_OK: return "OK";
    case CS_NOACK: return "OK, bez potw.";
    case CS_OTA: return String("aktual. ") + (otaPct < 0 ? 0 : otaPct) + "%";
    case CS_DNS: return "blad DNS";
    case CS_TCP: return String("port ") + fab.cloudPort + " zamkn.";
    case CS_TLS: return "blad certyf.";
    case CS_AUTH: return "logowanie odrz.";
  }
  return "?";
}

bool cloudFault() {
  if (!cloudOn() || !fabProgrammed() || cloudConnected() || cst == CS_OTA) return false;
  return millis() - downSinceMs > (uint32_t)cfg.cloudFaultMin * 60000UL;
}

void cloudStatusJson(JsonObject o) {
  o["en"] = (bool)cfg.cloudEn;  if (testUntil && cloudOn()) o["test"] = true;
  o["st"] = cloudStCode();
  o["msg"] = cloudStatusText();
  o["hw"] = cloudHwLabel();
  o["id"] = idOk();
  if (fabProgrammed()) { o["host"] = fab.cloudHost; o["port"] = fab.cloudPort; }
  if (cloudConnected() && sinceT > 100000) o["since"] = (int64_t)sinceT;
  o["sent"] = sentCnt;
  Lock l;
  o["buf"] = buf ? buf->size() : 0;
  o["lost"] = buf ? buf->lost() : 0;
  if (otaPct >= 0) o["ota"] = otaPct;
  o["fwMin"] = FW_MIN;
  o["cfgRev"] = cfg.cloudCfgRev;
  JsonArray a = o["cmds"].to<JsonArray>();
  for (uint8_t i = 0; i < lcCount; i++) {
    const LastCmd &c = lastCmds[(lcHead + 10 - 1 - i) % 10];
    JsonObject x = a.add<JsonObject>();
    x["t"] = c.t; x["name"] = c.name; x["res"] = c.res;
  }
}

// ---------------- publikacje ----------------
static bool pub(const char *sfx, const char *body, size_t len, bool retain) {
  char t[64];
  snprintf(t, sizeof(t), "%s%s", base, sfx);
  bool ok = mq.publish(t, (const uint8_t *)body, len, retain);
  if (ok) sentCnt++;
  return ok;
}
static bool pubStr(const char *sfx, const std::string &b, bool retain = false) { return pub(sfx, b.data(), b.size(), retain); }
static bool pubDoc(const char *sfx, JsonDocument &d, bool retain = false) { std::string b; serializeJson(d, b); return pubStr(sfx, b, retain); }

static void pubInfo() {
  JsonDocument d;
  envelope(d, -1);
  JsonObject x = d["data"].to<JsonObject>();
  const TankCal &c = cfg.tank[0];
  x["sn"] = fabSn(); x["hw_id"] = hwId; x["fw"] = FW_VERSION; x["fw_num"] = FW_NUM; x["fw_min"] = FW_MIN; x["fw_min_num"] = FW_MIN_NUM;
  x["hw"] = HW_VERSION; x["board"] = HW_NAME; x["mac"] = fabMac(); x["name"] = cfg.devName;
  x["height_cm"] = c.heightCm;
  if (cfg.tx[0].volEn) x["vol_l"] = roundf(tankTotalL(0));
  x["alarm_mode"] = cfg.alarmMode;
  JsonArray p = x["thr_pct"].to<JsonArray>();
  const float *ap = alarmPct();
  for (int i = 0; i < 3; i++) p.add(roundf(ap[i] * 10) / 10);
  x["hyst_cm"] = c.hyst; x["delay_s"] = c.delayS; x["drop_cm"] = cfg.tx[0].dropCm; x["drop_min"] = cfg.tx[0].dropMin;
  x["tele_s"] = cfg.cloudTeleS; x["cfg_rev"] = cfg.cloudCfgRev;
  JsonArray k = x["cfg_keys"].to<JsonArray>();
  for (size_t i = 0; i < CLOUD_CFG_KEYS_N; i++) k.add(CLOUD_CFG_KEYS[i]);
  JsonArray m = x["mail_rcpt"].to<JsonArray>();
  for (auto &r : cfg.rcpt) if (r.addr[0]) { JsonObject o = m.add<JsonObject>(); o["addr"] = r.addr; o["mask"] = r.mask; }
  pubDoc("info", d, true);
}

static void pubTele() {
  TankState s = tankGet(0);
  JsonDocument d;
  envelope(d, -1);
  JsonObject x = d["data"].to<JsonObject>();
  x["valid"] = s.valid;
  if (s.valid) { x["cm"] = roundf(s.cm * 10) / 10; x["pct"] = roundf(s.pct * 10) / 10; if (cfg.tx[0].volEn) x["l"] = roundf(s.vol); }
  x["mA"] = roundf(s.mA * 100) / 100;
  x["state"] = stateName(tankEffState(0));
  x["alarm"] = alarmActive; x["acked"] = alarmAcked; x["warn"] = warnActive;
  x["out"] = outState; x["di"] = diState; x["fault"] = faultBits();
  pubDoc("tele/tank", d);
  JsonDocument y;
  envelope(y, -1);
  JsonObject z = y["data"].to<JsonObject>();
  z["up_s"] = (uint32_t)(upMs() / 1000);
  z["heap"] = ESP.getFreeHeap(); z["heap_min"] = ESP.getMinFreeHeap(); z["psram"] = ESP.getFreePsram();
  z["net"] = ethHasIp() ? "eth" : "wifi";
  if (!ethHasIp() && wifiHasIp()) z["rssi"] = WiFi.RSSI();
  { Lock l; z["buf"] = buf->size(); z["lost"] = buf->lost(); }
  z["stack"] = uxTaskGetStackHighWaterMark(nullptr);
  pubDoc("tele/sys", y);
}

// ---------------- polecenia ----------------
static bool okPass(const char *s) {
  size_t n = strlen(s);
  if (n < 16 || n > 32) return false;
  for (size_t i = 0; i < n; i++) if (s[i] <= ' ' || s[i] > '~') return false;
  return true;
}

static bool isHex64(const char *s) {
  if (strlen(s) != 64) return false;
  for (int i = 0; i < 64; i++) if (!isxdigit((uint8_t)s[i])) return false;
  return true;
}

static void onCmd(const std::string &name, JsonDocument &in) {
  std::string cid = in["cid"] | "";
  if (cid.empty() || cid.size() > CidCache::MAX_CID) return;          // bez cid nie da sie odpowiedziec
  { Lock l; const std::string *r = cids.find(cid); if (r) { outPost("res", *r); return; } }   // powtorzenie - ten sam wynik
  const char *ex = cmdExpCheck(name, timeOk(), (int64_t)time(nullptr), in["exp"] | (int64_t)0);
  if (*ex) { result(name, cid, ex, timeOk() ? "" : "clock"); return; }
  JsonObjectConst data = in["data"];

  if (name == "sys/info") { infoReq = true; result(name, cid, "ok"); return; }
  if (name == "sys/log") {
    int n = data["n"] | 50; n = constrain(n, 1, 200);
    mainPost(MR_SYSLOG, "", cid, n); return;                          // karta SD tylko z petli glownej
  }
  if (name == "tank/ack") { mainPost(MR_ACK, "", cid); return; }
  if (name == "sys/reboot") { result(name, cid, "ok"); mainPost(MR_REBOOT, "", cid); return; }
  if (name == "sys/rotate") {
    const char *p = data["pass"] | "";
    if (!okPass(p)) { result(name, cid, "err", "pass"); return; }
    if (rotating) { result(name, cid, "busy"); return; }
    strlcpy(rotPass, p, sizeof(rotPass)); strlcpy(rotCid, cid.c_str(), sizeof(rotCid));
    rotating = true;                                                    // ponowne logowanie nowym haslem w cloudStep()
    noteCmd(name, "busy");
    return;
  }
  if (name == "sys/ota") {
    const char *url = data["url"] | "", *sha = data["sha256"] | "", *fw = data["fw"] | "";
    uint32_t size = data["size"] | 0u, num = data["num"] | 0u;
    bool down = data["downgrade"] | false;
    const esp_partition_t *np = esp_ota_get_next_update_partition(nullptr);
    if (ota.on || Update.isRunning()) { result(name, cid, "busy"); return; }
    if (strncmp(url, "https://", 8) || strlen(url) > 600 || !isHex64(sha) || !num || !*fw) { result(name, cid, "err", "args"); return; }
    if (!np || !size || size > np->size) { result(name, cid, "err", "size"); return; }
    std::string vc = otaVersionCheck(num, FW_NUM, FW_MIN_NUM, down);
    if (vc == "denied") { result(name, cid, "denied", "fw_min"); return; }
    if (vc.size()) { result(name, cid, "err", vc.c_str()); return; }
    ota = OtaReq{true, url, sha, fw, cid, size, num, down};
    JsonDocument r; r["phase"] = "download";
    outPost("res", resBody(cid, "busy", "", &r));
    noteCmd(name, "busy");
    logPost(String("Chmura: ") + (down ? "cofnięcie wersji " : "aktualizacja ") + FW_VERSION + " → " + fw + ", cid " + cid.c_str());
    return;
  }
  result(name, cid, "unsupported");
}

static void onMsg(char *topic, byte *payload, unsigned int len) {
  size_t bl = strlen(base);
  if (strncmp(topic, base, bl)) return;
  std::string sfx = topic + bl;
  JsonDocument in;
  if (deserializeJson(in, (const char *)payload, len)) return;          // const char* = kopia tresci (bufor klienta zostanie nadpisany)
  if (sfx == "ack") {
    if (strcmp(in["boot"] | "", bootId)) return;
    Lock l;
    if (in["upto"].is<uint32_t>()) buf->ackUpto(in["upto"].as<uint32_t>());
    for (JsonVariant v : in["seq"].as<JsonArray>()) buf->ack(v.as<uint32_t>());
    return;
  }
  if (sfx == "cfg") {
    int64_t rev = in["rev"] | (int64_t)-1;
    if (rev < 0 || rev == (int64_t)cfg.cloudCfgRev || rev == rejectedRev) return;
    std::string b((const char *)payload, len);
    mainPost(MR_CFG, b, "", (int32_t)rev);
    return;
  }
  if (sfx.rfind("cmd/", 0) == 0) onCmd(sfx.substr(4), in);
}

// ---------------- OTA z chmury ----------------
static void tryClear() {
  memset(&tryRec, 0, sizeof(tryRec));
  Preferences p; p.begin("cloud", false); p.remove("try"); p.end();
}

static const char *otaRun(std::string &code) {
  const esp_partition_t *np = esp_ota_get_next_update_partition(nullptr);
  if (ESP.getFreeHeap() < 60000 || ESP.getMaxAllocHeap() < 30000) { code = "mem"; return "busy"; }
  WiFiClientSecure hc;
  hc.useBuiltinCACertBundle();                          // pakiet glowny Mozilli z rdzenia ESP32
  HTTPClient http;
  if (!http.begin(hc, ota.url.c_str())) { code = "url"; return "err"; }
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int hc_ = http.GET();
  if (hc_ != 200) { http.end(); code = "net"; code += String(hc_).c_str(); return "err"; }
  int len = http.getSize();
  if (len != (int)ota.size) { http.end(); code = "size"; return "err"; }
  if (!Update.begin(ota.size, U_FLASH)) { http.end(); code = "flash"; return "err"; }
  otaSigBegin();
  FwTagScanner tag;
  mbedtls_sha256_context sc; mbedtls_sha256_init(&sc); mbedtls_sha256_starts(&sc, 0);
  uint8_t *b = (uint8_t *)malloc(4096);
  NetworkClient *s = http.getStreamPtr();
  uint32_t got = 0, t0 = millis(), tData = millis();
  bool ok = b != nullptr;
  if (!ok) code = "mem";
  while (ok && got < ota.size) {
    size_t av = s->available();
    if (!av) {
      if (!http.connected() || millis() - tData > OTA_IDLE_MS || millis() - t0 > OTA_TOTAL_MS) { ok = false; code = "net"; break; }
      vTaskDelay(pdMS_TO_TICKS(5)); continue;
    }
    int n = s->readBytes(b, min<size_t>(av, min<size_t>(4096, ota.size - got)));
    if (n <= 0) continue;
    tData = millis();
    otaSigFeed(b, n); tag.feed(b, n); mbedtls_sha256_update(&sc, b, n);
    if (Update.write(b, n) != (size_t)n) { ok = false; code = "flash"; break; }
    got += n;
    otaPct = (int)((uint64_t)got * 100 / ota.size);
  }
  free(b);
  http.end();
  uint8_t h[32]; mbedtls_sha256_finish(&sc, h); mbedtls_sha256_free(&sc);
  if (ok) {
    char hx[65]; for (int i = 0; i < 32; i++) sprintf(hx + 2 * i, "%02x", h[i]);
    String want = ota.sha.c_str(); want.toLowerCase();
    String se = otaSigFinish();
    if (want != hx) { ok = false; code = "sha"; }
    else if (se.length()) { ok = false; code = "sig"; }
    else if (!tag.found() || (uint32_t)tag.num() != ota.num) { ok = false; code = "ver"; }
    else if (otaVersionCheck(tag.num(), FW_NUM, FW_MIN_NUM, ota.down).size()) { ok = false; code = "ver"; }   // numer z pliku, nie z polecenia
  }
  if (!ok) { Update.abort(); return "err"; }
  if (!Update.end(true)) { code = "flash"; return "err"; }
  // proba nowej wersji: potwierdzenie po polaczeniu z chmura, inaczej powrot (cloudBootCheck / cloudStep)
  memset(&tryRec, 0, sizeof(tryRec));
  tryRec.active = 1; tryRec.down = ota.down;
  strlcpy(tryRec.part, np->label, sizeof(tryRec.part));
  strlcpy(tryRec.from, FW_VERSION, sizeof(tryRec.from)); strlcpy(tryRec.to, ota.fw.c_str(), sizeof(tryRec.to));
  strlcpy(tryRec.cid, ota.cid.c_str(), sizeof(tryRec.cid));
  tryRec.fromNum = FW_NUM; tryRec.toNum = ota.num;
  Preferences p; p.begin("cloud", false); p.putBytes("try", &tryRec, sizeof(tryRec)); p.end();
  return "ok";
}

static void doOta() {
  // zapowiedz (res busy) juz w kolejce - wyslij, potem rozlacz MQTT na czas pobierania (pamiec na drugie polaczenie TLS)
  { Lock l; while (!outbox.empty() && mq.connected()) { pubStr(outbox.front().sfx.c_str(), outbox.front().body, outbox.front().retain); outbox.pop_front(); } }
  if (mq.connected()) { pubStr("status", "{\"st\":\"offline\",\"why\":\"ota\"}", true); mq.disconnect(); }
  net.stop();
  cst = CS_OTA; otaPct = 0;
  std::string code;
  const char *r = otaRun(code);
  otaPct = -1;
  std::string name = "sys/ota", cid = ota.cid;
  ota.on = false;
  if (!strcmp(r, "ok")) {
    noteCmd(name, "restart");
    logPost(String("Chmura: plik ") + tryRec.to + " sprawdzony (SHA-256, podpis, wersja) – restart");
    mainPost(MR_REBOOT, "", cid);
    cst = CS_CONN; nextTry = millis() + 60000;
    return;
  }
  result(name, cid, r, code.c_str());
  JsonDocument e; e["ota"] = "failed"; e["code"] = code; e["fw"] = FW_VERSION;
  evtPush("sys", e);
  cst = CS_CONN; nextTry = millis();
}

// ---------------- polaczenie ----------------
static void failRetry(uint8_t s, uint32_t ms) {
  cst = s; fails++;
  nextTry = millis() + ms;
  if (s == CS_DNS || s == CS_TCP) retryMs = min(retryMs * 2, RETRY_MAX_MS);
}

static void doConnect() {
  cst = CS_CONN;
  IPAddress ip;
  if (!WiFi.hostByName(fab.cloudHost, ip)) { failRetry(CS_DNS, retryMs); return; }
  net.setCACert(CLOUD_CA_PEM);
  net.setTimeout(10);
  net.setHandshakeTimeout(20);
  if (!net.connect(fab.cloudHost, fab.cloudPort)) {
    char e[80]; int ec = net.lastError(e, sizeof(e));
    // bledy mbedTLS (certyfikat, uzgadnianie) maja kody ujemne < -0x1000; blad gniazda = -1 / 0
    bool tls = ec < -0x1000;
    net.stop();
    if (tls) failRetry(CS_TLS, RETRY_TLS_MS); else failRetry(CS_TCP, retryMs);
    return;
  }
  const char *pass = rotating ? rotPass : fabCloudPass();
  char lwt[64]; snprintf(lwt, sizeof(lwt), "%sstatus", base);
  mq.setServer(fab.cloudHost, fab.cloudPort);
  bool ok = mq.connect(hwId, hwId, pass, lwt, 1, true, "{\"st\":\"offline\"}", true);
  if (!ok) {
    int s = mq.state();
    net.stop();
    if (s == MQTT_CONNECT_BAD_CREDENTIALS || s == MQTT_CONNECT_UNAUTHORIZED) {
      if (rotating) {                                             // nowe haslo nie dziala - wracamy do starego
        rotating = false;
        result("sys/rotate", rotCid, "err", "auth");
        nextTry = millis(); cst = CS_CONN; return;
      }
      failRetry(CS_AUTH, RETRY_AUTH_MS);
      logPost("Chmura: logowanie odrzucone");
    } else failRetry(CS_TCP, retryMs);
    return;
  }
  if (rotating) {
    rotating = false;
    if (fabCloudPassCommit(rotPass)) result("sys/rotate", rotCid, "ok");
    else result("sys/rotate", rotCid, "err", "nvs");
    memset(rotPass, 0, sizeof(rotPass));
  }
  cst = CS_OK; fails = 0; retryMs = RETRY_MIN_MS; sinceT = time(nullptr);
  char on[40]; snprintf(on, sizeof(on), "{\"st\":\"online\",\"boot\":\"%s\"}", bootId);
  pubStr("status", on, true);
  pubInfo();
  char t[64];
  for (const char *s : {"cmd/#", "ack", "cfg"}) { snprintf(t, sizeof(t), "%s%s", base, s); mq.subscribe(t, 1); }
  { Lock l; buf->restart(); }
  lastTele = 0;
  logPost(String("Chmura: połączono z ") + fab.cloudHost + ":" + fab.cloudPort);
  // potwierdzenie nowej wersji po OTA z chmury
  if (pendingVerify) {
    pendingVerify = false;
    esp_ota_mark_app_valid_cancel_rollback();
    JsonDocument e; e["ota"] = tryRec.down ? "downgraded" : "installed"; e["fw"] = FW_VERSION; e["from"] = tryRec.from;
    evtPush("sys", e);
    JsonDocument r; r["fw"] = FW_VERSION; r["from"] = tryRec.from;
    result("sys/ota", tryRec.cid, "ok", "", &r);
    logPost(String("Chmura: ") + (tryRec.down ? "cofnięto wersję " : "zaktualizowano ") + tryRec.from + " → " + FW_VERSION + " (potwierdzone)");
    tryClear();
  }
  if (rolledBack && !tryAnnounced) {
    tryAnnounced = true;
    JsonDocument e; e["ota"] = "rollback"; e["fw"] = FW_VERSION; e["failed"] = tryRec.to;
    evtPush("sys", e);
    result("sys/ota", tryRec.cid, "err", "rollback");
    tryClear();
  }
}

static void cloudStep() {
  if (applyReq) {
    applyReq = false;
    if (mq.connected()) { pubStr("status", "{\"st\":\"offline\"}", true); mq.disconnect(); }
    net.stop();
    if (!cloudOn()) { Lock l; buf->clear(); outbox.clear(); }
    fails = 0; retryMs = RETRY_MIN_MS; nextTry = millis(); downSinceMs = millis();
  }
  if (!cloudOn()) {                                                 // takze koniec testu PROV-CLOUDTEST
    if (mq.connected()) { pubStr("status", "{\"st\":\"offline\"}", true); mq.disconnect(); net.stop(); }
    cst = CS_OFF; return;
  }
  if (!idOk()) { cst = CS_NOID; return; }
  if (pendingVerify && millis() > OTA_CONFIRM_MS) {
    logPost(String("Chmura: wersja ") + FW_VERSION + " nie połączyła się w 10 min – powrót do " + tryRec.from);
    vTaskDelay(pdMS_TO_TICKS(1500));                              // czas na zapis w dzienniku
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  if (!ethHasIp() && !wifiHasIp()) {
    if (mq.connected()) mq.disconnect();
    net.stop();
    if (cloudConnected()) downSinceMs = millis();
    cst = CS_NONET; return;
  }
  if (ota.on) { doOta(); return; }
  if (rotating && mq.connected()) { mq.disconnect(); net.stop(); nextTry = millis(); }
  if (!mq.connected()) {
    if (cloudConnected()) { logPost("Chmura: rozłączono"); downSinceMs = millis(); cst = CS_CONN; }
    if ((int32_t)(millis() - nextTry) < 0) return;
    doConnect();
    return;
  }
  mq.loop();
  // publikacje z kolejki (wyniki polecen, czesci dziennika)
  for (int i = 0; i < 4 && mq.connected(); i++) {
    Out o;
    { Lock l; if (outbox.empty()) break; o = outbox.front(); outbox.pop_front(); }
    pubStr(o.sfx.c_str(), o.body, o.retain);
  }
  if (infoReq) { infoReq = false; pubInfo(); }
  // zdarzenia czekajace na ack
  for (int i = 0; i < 4 && mq.connected(); i++) {
    std::string name; PStr body; uint32_t seq;
    {
      Lock l;
      const EvtBuffer::Item *it = buf->next(millis());
      if (!it) break;
      name = "evt/" + it->name; body = it->body; seq = it->seq;
      buf->markSent(seq, millis());
    }
    pub(name.c_str(), body.data(), body.size(), false);
  }
  if (!lastTele || millis() - lastTele >= (uint32_t)cfg.cloudTeleS * 1000UL) { lastTele = millis(); pubTele(); }
  { Lock l; cst = buf->maxStep() >= 2 ? CS_NOACK : CS_OK; }
}

static void cloudTask(void *) {
  for (;;) {
    cloudStep();
    vTaskDelay(pdMS_TO_TICKS(cloudConnected() ? 20 : 200));
  }
}

// ---------------- start ----------------
void cloudBootCheck() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  strlcpy(hwId, hwFromMac(m).c_str(), sizeof(hwId));
  snprintf(base, sizeof(base), MODULE "/%s/v1/", hwId);
  snprintf(bootId, sizeof(bootId), "%08lx", (unsigned long)esp_random());
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t s = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(run, &s);
  Preferences p;
  p.begin("cloud", true);
  bool has = p.getBytesLength("try") == sizeof(tryRec) && p.getBytes("try", &tryRec, sizeof(tryRec)) == sizeof(tryRec) && tryRec.active;
  p.end();
  if (has) {
    if (!strcmp(run->label, tryRec.part)) {
      if (s == ESP_OTA_IMG_PENDING_VERIFY) { pendingVerify = true; Serial.printf("Chmura: proba wersji %s (z %s) – potwierdzenie po polaczeniu\n", FW_VERSION, tryRec.from); }
      else tryClear();
    } else {
      rolledBack = true;                                           // dziala inna partycja niz wgrana = powrot do poprzedniej
      Serial.printf("Chmura: powrot do wersji %s po nieudanej probie %s\n", FW_VERSION, tryRec.to);
    }
  }
  if (s == ESP_OTA_IMG_PENDING_VERIFY && !pendingVerify) esp_ota_mark_app_valid_cancel_rollback();   // OTA lokalne / USB
  Serial.printf("Znacznik wersji: %s\n", MU_FW_TAG);    // uzycie = znacznik zostaje w obrazie (linker)
}

void cloudBegin() {
  if (mux) return;
  mux = xSemaphoreCreateRecursiveMutex();
  buf = new EvtBuffer(200, 120000, 4);
  mq.setBufferSize(1280);
  mq.setKeepAlive(60);
  mq.setSocketTimeout(10);
  mq.setCallback(onMsg);
  downSinceMs = millis();
  if (rolledBack) logEvent("CHMURA", String("Nieudana próba wersji ") + tryRec.to + " – przywrócono " + FW_VERSION);
  if (pendingVerify) logEvent("CHMURA", String("Nowa wersja ") + FW_VERSION + " z chmury – czeka na połączenie (10 min)");
  xTaskCreatePinnedToCore(cloudTask, "cloud", 12288, nullptr, 1, nullptr, 0);
}

void cloudTest(uint16_t s) {
  testUntil = s ? millis() + s * 1000UL : 0;
  if (s && !testUntil) testUntil = 1;
  applyReq = true;
}

void cloudApply() {
  if (!cloudOn() && pendingVerify) {                              // wylaczenie na miejscu konczy probe - wersja zostaje
    pendingVerify = false;
    esp_ota_mark_app_valid_cancel_rollback();
    tryClear();
    logEvent("CHMURA", String("Wersja ") + FW_VERSION + " zatwierdzona lokalnie (chmura wyłączona)");
  }
  applyReq = true;
}

void cloudOnLog(const char *type, const String &msg) {
  if (!mux || !cloudOn() || !idOk()) return;
  JsonDocument d;
  d["type"] = type; d["msg"] = msg;
  const char *name = "sys";
  if (!strcmp(type, "ALARM")) {
    name = "alarm";
    d["state"] = stateName(tankEffState(0)); d["active"] = alarmActive; d["acked"] = alarmAcked;
  } else if (!strcmp(type, "TANKOWANIE") || !strcmp(type, "KALIBRACJA")) name = "tank";
  evtPush(name, d);
}

// ---------------- petla glowna ----------------
static void rejectCfg(int32_t rev, const String &field, const String &why) {
  rejectedRev = rev;
  JsonDocument e; e["cfg"] = "rejected"; e["rev"] = rev; if (field.length()) e["field"] = field; e["msg"] = why;
  evtPush("sys", e);
  logEvent("CHMURA", String("Odrzucono ustawienia z chmury (rev ") + rev + "): " + why);
}

static void applyCfg(const std::string &raw, int32_t rev) {
  JsonDocument in;
  if (deserializeJson(in, raw)) { rejectCfg(rev, "", "niepoprawny JSON"); return; }
  JsonObjectConst d = in["data"];
  if (d.isNull()) { rejectCfg(rev, "", "brak data"); return; }
  for (JsonPairConst p : d) if (!cloudCfgKeyAllowed(p.key().c_str())) { rejectCfg(rev, p.key().c_str(), "pole spoza listy"); return; }
  int tele = cfg.cloudTeleS;
  if (!d["tele_s"].isNull()) {
    tele = d["tele_s"] | -1;
    if (tele < 30 || tele > 600) { rejectCfg(rev, "tele_s", "interwał 30–600 s"); return; }
  }
  if (!d["name"].isNull() && (!d["name"].is<const char *>() || !strlen(d["name"].as<const char *>()) || strlen(d["name"].as<const char *>()) >= sizeof(cfg.devName))) {
    rejectCfg(rev, "name", "nazwa 1–23 znaki"); return;
  }
  JsonDocument s;
  if (d["name"].is<const char *>()) s["device"]["name"] = d["name"];
  bool tank = false;
  JsonObject t = s["tanks"].to<JsonArray>().add<JsonObject>();
  auto tk = [&](const char *src, const char *dst) { if (!d[src].isNull()) { t[dst] = d[src]; tank = true; } };
  tk("alarm_mode", "alarmMode"); tk("thr_pct", "pct"); tk("hyst_cm", "hyst"); tk("delay_s", "delay"); tk("drop_cm", "dropCm"); tk("drop_min", "dropMin");
  if (!tank) s.remove("tanks");
  if (!d["mail_rcpt"].isNull()) {
    if (!d["mail_rcpt"].is<JsonArrayConst>() || d["mail_rcpt"].size() > MAIL_RCPT_N) { rejectCfg(rev, "mail_rcpt", "lista do 5 odbiorców"); return; }
    s["mail"]["rcpt"] = d["mail_rcpt"];
  }
  if (tank && !d["drop_cm"].isNull() && !(d["drop_cm"].as<float>() > 0)) { rejectCfg(rev, "drop_cm", "spadek > 0 cm"); return; }
  String err = s.size() ? cfgFromJson(s.as<JsonObjectConst>()) : String("");
  if (err.length()) { rejectCfg(rev, "", err); return; }
  cfg.cloudTeleS = tele;
  cfg.cloudCfgRev = (uint32_t)rev;
  cfgSave();
  if (tank) sensorsReset();
  String keys;
  JsonDocument e; e["cfg"] = "applied"; e["rev"] = rev;
  JsonArray ka = e["keys"].to<JsonArray>();
  for (JsonPairConst p : d) { ka.add(p.key().c_str()); if (keys.length()) keys += ", "; keys += p.key().c_str(); }
  evtPush("sys", e);
  logEvent("USTAWIENIA", String("Zmieniono z chmury (rev ") + rev + "): " + keys);
  infoReq = true;
}

// Ostatnie n wpisow dziennika (karta SD, biezacy i poprzedni miesiac; bez karty - pamiec RAM) jako evt/log w czesciach
static void sysLog(const std::string &cid, int n) {
  std::vector<String> lines;
  if (sdMounted && timeValid()) {
    time_t now = time(nullptr);
    for (int mo = 0; mo < 2 && (int)lines.size() < n; mo++) {
      struct tm tm; localtime_r(&now, &tm);
      tm.tm_mon -= mo; mktime(&tm);
      char fn[32]; strftime(fn, sizeof(fn), "/events/%Y-%m.csv", &tm);
      File f = SD.open(fn);
      if (!f) continue;
      size_t want = (size_t)(n - lines.size()) * 140 + 200;
      if (f.size() > want) f.seek(f.size() - want);
      std::vector<String> part;
      bool first = f.position() > 0;
      while (f.available()) {
        String l = f.readStringUntil('\n'); l.trim();
        if (first) { first = false; continue; }                       // niepelna linia po seek
        if (l.length() && !l.startsWith("data;")) part.push_back(l);
        wdtFeed();
      }
      f.close();
      size_t take = min(part.size(), (size_t)(n - lines.size()));
      lines.insert(lines.begin(), part.end() - take, part.end());
    }
  }
  if (lines.empty()) {
    for (uint8_t i = 0; i < evCount && (int)lines.size() < n; i++) {
      const EventRec &e = evRam[(evHead + EV_RAM - 1 - i) % EV_RAM];
      char t[24]; fmtTime(t, sizeof(t), "%Y-%m-%d;%H:%M:%S", e.t);
      lines.insert(lines.begin(), String(t) + ";" + e.type + ";" + e.msg);
    }
  }
  // podzial na czesci mieszczace sie w limicie
  std::vector<std::pair<size_t, size_t>> parts;
  size_t i0 = 0, bytes = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    if (bytes + lines[i].length() + 4 > 700 && i > i0) { parts.push_back({i0, i}); i0 = i; bytes = 0; }
    bytes += lines[i].length() + 4;
  }
  if (i0 < lines.size() || parts.empty()) parts.push_back({i0, lines.size()});
  for (size_t p = 0; p < parts.size(); p++) {
    JsonDocument d;
    d["msg"] = cid; d["part"] = p + 1; d["parts"] = parts.size();
    JsonArray a = d["lines"].to<JsonArray>();
    for (size_t i = parts[p].first; i < parts[p].second; i++) a.add(lines[i]);
    evtPush("log", d);
  }
  JsonDocument r; r["n"] = lines.size(); r["parts"] = parts.size();
  result("sys/log", cid, "ok", "", &r);
}

void cloudLoop() {
  for (int k = 0; k < 4; k++) {
    MainReq m;
    if (!mux) return;
    { Lock l; if (mainQ.empty()) return; m = mainQ.front(); mainQ.pop_front(); }
    switch (m.kind) {
      case MR_LOG: logEvent("CHMURA", m.a.c_str()); break;
      case MR_ACK: {
        bool was = alarmActive && !alarmAcked;
        alarmAck(3);
        JsonDocument r; r["was_active"] = was; r["acked"] = alarmAcked;
        result("tank/ack", m.cid, "ok", "", &r);
        break;
      }
      case MR_REBOOT: logEvent("CHMURA", "Restart z polecenia chmury"); scheduleReboot(3000); break;
      case MR_CFG: applyCfg(m.a, m.n); break;
      case MR_SYSLOG: sysLog(m.cid, m.n); break;
    }
  }
}
