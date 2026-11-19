// Testy logiki protokolu chmury (PC): g++ -std=c++17 -I../02_Monitor_App cloudproto_test.cpp ../02_Monitor_App/cloudproto.cpp && ./a.out
#include "cloudproto.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main() {
  const uint8_t mac[6] = {0x94, 0xA9, 0x90, 0x2B, 0x4A, 0xCC};          // plytka testowa (#177)
  std::string h = hwFromMac(mac);
  CHECK(hwLabel(h) == "4MN6-82PJ-PCX");
  CHECK(hwValid(h));
  std::string bad = h; bad[3] = bad[3] == '0' ? '1' : '0';
  CHECK(!hwValid(bad));
  CHECK(!hwValid("ZZZZZZZZZZZ"));                                        // > 48 bitow
  // bufor zdarzen
  EvtBuffer b(3, 10000, 2);
  for (int i = 0; i < 4; i++) { uint32_t s = b.allocSeq(); b.push(s, "alarm", PStr("x")); }
  CHECK(b.size() == 3 && b.lost() == 1);
  const EvtBuffer::Item *it = b.next(0);
  CHECK(it && it->seq == 1);
  b.markSent(1, 0);
  it = b.next(0); CHECK(it && it->seq == 2);
  b.markSent(2, 0);
  CHECK(b.next(1000) == nullptr);                                        // okno 2, oba w drodze
  CHECK(b.next(30000) && b.next(30000)->seq == 1);                       // ponowienie po 30 s
  CHECK(b.ackUpto(2) == 2 && b.size() == 1);
  CHECK(b.maxStep() == 0);
  // cid
  CidCache c;
  for (int i = 0; i < 20; i++) c.put("c" + std::to_string(i), "r" + std::to_string(i));
  CHECK(!c.find("c0") && c.find("c19") && *c.find("c19") == "r19");
  // exp / zegar
  CHECK(!strcmp(cmdExpCheck("sys/reboot", false, 0, 100), "expired"));
  CHECK(!strcmp(cmdExpCheck("sys/info", false, 0, 0), ""));
  CHECK(!strcmp(cmdExpCheck("tank/ack", true, 200, 100), "expired"));
  CHECK(!strcmp(cmdExpCheck("sys/ota", true, 100, 0), "expired"));
  CHECK(!strcmp(cmdExpCheck("sys/ota", true, 100, 200), ""));
  // wersje OTA
  CHECK(otaVersionCheck(61, 60, 60, false) == "");
  CHECK(otaVersionCheck(60, 61, 60, false) == "ver");
  CHECK(otaVersionCheck(60, 61, 60, true) == "");
  CHECK(otaVersionCheck(57, 61, 60, true) == "denied");
  CHECK(otaVersionCheck(61, 61, 60, false) == "ver");
  // znacznik wersji, takze na granicy kawalkow i z falszywym trafieniem przed nim
  const char img[] = "abc MUFWTAG1:\0xx; zzz MUFWTAG1:61:0.6.1-dev; tail";
  for (size_t cut = 1; cut < sizeof(img) - 1; cut++) {
    FwTagScanner s;
    s.feed((const uint8_t *)img, cut); s.feed((const uint8_t *)img + cut, sizeof(img) - 1 - cut);
    CHECK(s.found() && s.num() == 61 && s.version() == "0.6.1-dev");
  }
  FwTagScanner none; none.feed((const uint8_t *)"no tag here;", 12); CHECK(!none.found());
  // lista cfg
  CHECK(cloudCfgKeyAllowed("thr_pct") && !cloudCfgKeyAllowed("wifi") && !cloudCfgKeyAllowed("drop_en"));
  printf(fails ? "NIEPOWODZENIE: %d\n" : "OK - wszystkie testy\n", fails);
  return fails != 0;
}
