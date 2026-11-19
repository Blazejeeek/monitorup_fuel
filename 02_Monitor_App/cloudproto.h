// MonitorUp - logika protokolu Zdalio v1 bez zaleznosci od Arduino (0.6.0, karta #179).
// Testy na PC: tests/cloudproto_test.cpp (g++). Kontrakt: protokol Zdalio v1 (numer hw_id - rozdz. 2,
// bufor zdarzen i ack - 5.2, cid/exp - 5.4). Czesc kodu przeniesiona z firmware'u e-drukarki (#131, proto.cpp).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <string>

#ifdef ARDUINO
#include <esp_heap_caps.h>
// Tresci zdarzen w PSRAM (bufor do 200 zdarzen nie zabiera pamieci wewnetrznej potrzebnej dla TLS)
template <typename T> struct PsAlloc {
  using value_type = T;
  PsAlloc() = default;
  template <typename U> PsAlloc(const PsAlloc<U> &) {}
  T *allocate(size_t n) {
    void *p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n * sizeof(T));
    return (T *)p;
  }
  void deallocate(T *p, size_t) { free(p); }
  template <typename U> bool operator==(const PsAlloc<U> &) const { return true; }
  template <typename U> bool operator!=(const PsAlloc<U> &) const { return false; }
};
using PStr = std::basic_string<char, std::char_traits<char>, PsAlloc<char>>;
#else
using PStr = std::string;
#endif

constexpr size_t CLOUD_MAX_PAYLOAD = 1024;     // limit tresci (protokol rozdz. 4)
constexpr size_t HW_ID_LEN = 11;               // 10 znakow Crockford Base32 + znak kontrolny

// ---------- numer egzemplarza hw_id (zgodny z produkcja.py i iot.devices) ----------
std::string hwFromMac(const uint8_t mac[6]);
bool hwValid(const std::string &id);
std::string hwLabel(const std::string &id);    // XXXX-XXXX-XXX

// ---------- zdarzenia czekajace na ack ----------
class EvtBuffer {
 public:
  static constexpr uint32_t RETRY_FIRST_MS = 30000, RETRY_MAX_MS = 300000;
  struct Item { uint32_t seq; std::string name; PStr body; bool sent; uint8_t step; uint32_t dueMs; };
  EvtBuffer(size_t maxItems, size_t maxBytes, size_t window) : maxItems_(maxItems), maxBytes_(maxBytes), window_(window) {}
  uint32_t allocSeq() { return next_++; }
  void push(uint32_t seq, const std::string &name, PStr &&body);
  const Item *next(uint32_t nowMs) const;        // najstarsze do wyslania teraz albo nullptr
  void markSent(uint32_t seq, uint32_t nowMs);
  size_t ackUpto(uint32_t upto);
  size_t ack(uint32_t seq);
  void restart();                                // po ponownym polaczeniu: wszystko do wyslania od nowa
  void clear() { q_.clear(); bytes_ = 0; }
  size_t size() const { return q_.size(); }
  uint32_t lost() const { return lost_; }
  uint8_t maxStep() const;                       // ile razy najstarsze zdarzenie wyslano bez ack
 private:
  std::deque<Item> q_;
  size_t maxItems_, maxBytes_, window_, bytes_ = 0;
  uint32_t next_ = 0, lost_ = 0;
};

// ---------- pamiec ostatnich polecen (cid) ----------
class CidCache {
 public:
  static constexpr size_t CAP = 16, MAX_CID = 40;
  const std::string *find(const std::string &cid) const;
  void put(const std::string &cid, const std::string &result);
 private:
  struct E { std::string cid, result; };
  std::deque<E> q_;
};

// ---------- polecenia ----------
// Czy polecenie zmienia stan sterownika (wymaga wiarygodnego zegara i waznego exp)
bool cmdNeedsClock(const std::string &name);
// "" = mozna wykonac, inaczej status res: "expired"
const char *cmdExpCheck(const std::string &name, bool timeOk, int64_t now, int64_t exp);

// Decyzja o aktualizacji z chmury (rozdz. 4.3). Zwraca "" (OK) albo kod bledu: "ver" (starsza/ta sama bez downgrade),
// "denied" (ponizej fw_min), "size".
std::string otaVersionCheck(uint32_t num, uint32_t curNum, uint32_t fwMin, bool downgrade);

// Znacznik wersji w obrazie programu: "MUFWTAG1:<num>:<wersja>;" (stala w cloud.cpp). Skaner strumieniowy
// pozwala porownac numer z polecenia z numerem faktycznie zapisanym w pobranym pliku.
class FwTagScanner {
 public:
  void feed(const uint8_t *d, size_t n);
  bool found() const { return num_ >= 0; }
  int32_t num() const { return num_; }
  const std::string &version() const { return ver_; }
 private:
  std::string win_;
  int32_t num_ = -1;
  std::string ver_;
};

// Ustawienia z chmury (rozdz. 4.5) - lista zamknieta. Klucze dozwolone:
extern const char *const CLOUD_CFG_KEYS[];
extern const size_t CLOUD_CFG_KEYS_N;
bool cloudCfgKeyAllowed(const char *key);
