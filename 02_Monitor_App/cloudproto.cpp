// MonitorUp - logika protokolu Zdalio v1, opis w cloudproto.h
#include "cloudproto.h"
#include <string.h>
#include <stdlib.h>

static const char ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford Base32

static int luhn32(const int *d, int n) {
  int sum = 0, factor = 2;
  for (int i = n - 1; i >= 0; i--) {
    int a = factor * d[i];
    sum += a / 32 + a % 32;
    factor = 3 - factor;
  }
  return (32 - sum % 32) % 32;
}

static int symbol(char c) {
  for (int i = 0; i < 32; i++) if (ALPHABET[i] == c) return i;
  return -1;
}

std::string hwFromMac(const uint8_t mac[6]) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) v = (v << 8) | mac[i];
  std::string s(HW_ID_LEN, '0');
  int d[10];
  for (int i = 9; i >= 0; i--, v >>= 5) { d[i] = (int)(v & 31); s[i] = ALPHABET[d[i]]; }
  s[10] = ALPHABET[luhn32(d, 10)];
  return s;
}

bool hwValid(const std::string &id) {
  if (id.size() != HW_ID_LEN) return false;
  uint64_t v = 0;
  int d[10];
  for (size_t i = 0; i < 10; i++) {
    d[i] = symbol(id[i]);
    if (d[i] < 0) return false;
    v = (v << 5) | (uint64_t)d[i];
  }
  if (v >> 48) return false;
  return symbol(id[10]) == luhn32(d, 10);
}

std::string hwLabel(const std::string &id) {
  if (id.size() != HW_ID_LEN) return id;
  return id.substr(0, 4) + "-" + id.substr(4, 4) + "-" + id.substr(8);
}

// ---------------- EvtBuffer ----------------
void EvtBuffer::push(uint32_t seq, const std::string &name, PStr &&body) {
  bytes_ += body.size();
  q_.push_back(Item{seq, name, std::move(body), false, 0, 0});
  while (q_.size() > maxItems_ || bytes_ > maxBytes_) {
    bytes_ -= q_.front().body.size();
    q_.pop_front();
    lost_++;
  }
}

const EvtBuffer::Item *EvtBuffer::next(uint32_t nowMs) const {
  size_t n = 0;
  for (const Item &it : q_) {
    if (n++ >= window_) break;
    if (!it.sent || (int32_t)(nowMs - it.dueMs) >= 0) return &it;
  }
  return nullptr;
}

void EvtBuffer::markSent(uint32_t seq, uint32_t nowMs) {
  for (Item &it : q_) {
    if (it.seq != seq) continue;
    uint32_t delay = it.step >= 4 ? RETRY_MAX_MS : RETRY_FIRST_MS << it.step;
    if (delay > RETRY_MAX_MS) delay = RETRY_MAX_MS;
    if (it.step < 255) it.step++;
    it.sent = true;
    it.dueMs = nowMs + delay;
    return;
  }
}

size_t EvtBuffer::ackUpto(uint32_t upto) {
  size_t n = 0;
  for (auto it = q_.begin(); it != q_.end();) {
    if (it->seq <= upto) { bytes_ -= it->body.size(); it = q_.erase(it); n++; }
    else ++it;
  }
  return n;
}

size_t EvtBuffer::ack(uint32_t seq) {
  for (auto it = q_.begin(); it != q_.end(); ++it) {
    if (it->seq != seq) continue;
    bytes_ -= it->body.size();
    q_.erase(it);
    return 1;
  }
  return 0;
}

void EvtBuffer::restart() { for (Item &it : q_) { it.sent = false; it.step = 0; } }

uint8_t EvtBuffer::maxStep() const { return q_.empty() ? 0 : q_.front().step; }

// ---------------- CidCache ----------------
const std::string *CidCache::find(const std::string &cid) const {
  for (const E &e : q_) if (e.cid == cid) return &e.result;
  return nullptr;
}

void CidCache::put(const std::string &cid, const std::string &result) {
  for (E &e : q_) if (e.cid == cid) { e.result = result; return; }
  q_.push_back(E{cid, result});
  while (q_.size() > CAP) q_.pop_front();
}

// ---------------- polecenia ----------------
bool cmdNeedsClock(const std::string &name) {
  return name == "sys/reboot" || name == "sys/ota" || name == "sys/rotate";
}

const char *cmdExpCheck(const std::string &name, bool timeOk, int64_t now, int64_t exp) {
  if (!timeOk) return cmdNeedsClock(name) ? "expired" : "";
  if (exp > 0 && now > exp) return "expired";
  if (exp <= 0 && cmdNeedsClock(name)) return "expired";     // polecenie zmieniajace stan musi miec termin waznosci
  return "";
}

std::string otaVersionCheck(uint32_t num, uint32_t curNum, uint32_t fwMin, bool downgrade) {
  if (num < fwMin) return "denied";
  if (num <= curNum && !downgrade) return "ver";
  return "";
}

// ---------------- znacznik wersji w obrazie ----------------
void FwTagScanner::feed(const uint8_t *d, size_t n) {
  if (found()) return;
  static const char TAG[] = "MUFWTAG1:";
  const size_t TL = sizeof(TAG) - 1, KEEP = 64;
  win_.append((const char *)d, n);
  size_t p = win_.find(TAG);
  while (p != std::string::npos) {
    size_t e = win_.find(';', p);
    if (e == std::string::npos) break;                          // reszta w nastepnym kawalku
    std::string body = win_.substr(p + TL, e - p - TL);           // <num>:<wersja>
    size_t c = body.find(':');
    if (c != std::string::npos && c > 0 && c < 6) {
      bool digits = true;
      for (size_t i = 0; i < c; i++) if (body[i] < '0' || body[i] > '9') digits = false;
      if (digits && body.size() - c - 1 < 24) { num_ = atoi(body.substr(0, c).c_str()); ver_ = body.substr(c + 1); return; }
    }
    p = win_.find(TAG, p + 1);
  }
  if (win_.size() > KEEP) win_.erase(0, win_.size() - KEEP);
}

// ---------------- ustawienia z chmury ----------------
const char *const CLOUD_CFG_KEYS[] = {"name", "alarm_mode", "thr_pct", "hyst_cm", "delay_s", "drop_cm", "drop_min", "tele_s", "mail_rcpt"};
const size_t CLOUD_CFG_KEYS_N = sizeof(CLOUD_CFG_KEYS) / sizeof(CLOUD_CFG_KEYS[0]);

bool cloudCfgKeyAllowed(const char *key) {
  for (size_t i = 0; i < CLOUD_CFG_KEYS_N; i++) if (!strcmp(key, CLOUD_CFG_KEYS[i])) return true;
  return false;
}
