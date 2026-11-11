// MonitorUp - szyfrowanie kopii ustawien z haslami: AES-256-GCM, klucz z PBKDF2-SHA256 (mbedTLS)
#include "crypt.h"
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

static const char *AAD = "monitorup-backup-v1";

static String b64(const uint8_t *p, size_t n) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, p, n);
  String s;
  char *buf = (char *)malloc(olen + 1);
  if (!buf) return s;
  if (mbedtls_base64_encode((uint8_t *)buf, olen, &olen, p, n) == 0) { buf[olen] = 0; s = buf; }
  free(buf);
  return s;
}

// zwraca liczbe bajtow albo -1
static int unb64(const char *s, uint8_t *out, size_t cap) {
  size_t olen = 0;
  if (!s || mbedtls_base64_decode(out, cap, &olen, (const uint8_t *)s, strlen(s)) != 0) return -1;
  return (int)olen;
}

static bool deriveKey(const String &pass, const uint8_t *salt, size_t saltLen, uint32_t iter, uint8_t key[32]) {
  esp_task_wdt_reset();
  int r = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const uint8_t *)pass.c_str(), pass.length(), salt, saltLen, iter, 32, key);
  esp_task_wdt_reset();
  return r == 0;
}

String backupEncrypt(const String &plain, const String &pass, JsonObject out) {
  if (pass.length() < 8) return "Hasło kopii musi mieć min. 8 znaków";
  uint8_t salt[16], iv[12], tag[16], key[32];
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(iv, sizeof(iv));
  if (!deriveKey(pass, salt, sizeof(salt), BACKUP_KDF_ITER, key)) return "Błąd wyprowadzenia klucza";
  size_t n = plain.length();
  uint8_t *ct = (uint8_t *)malloc(n ? n : 1);
  if (!ct) { memset(key, 0, sizeof(key)); return "Za mało pamięci"; }
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!r) r = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, sizeof(iv), (const uint8_t *)AAD, strlen(AAD),
                                        (const uint8_t *)plain.c_str(), ct, sizeof(tag), tag);
  mbedtls_gcm_free(&g);
  memset(key, 0, sizeof(key));
  if (r) { free(ct); return "Błąd szyfrowania"; }
  out["enc"] = "AES-256-GCM";
  out["kdf"] = "PBKDF2-SHA256";
  out["iter"] = BACKUP_KDF_ITER;
  out["salt"] = b64(salt, sizeof(salt));
  out["iv"] = b64(iv, sizeof(iv));
  out["tag"] = b64(tag, sizeof(tag));
  out["data"] = b64(ct, n);
  free(ct);
  return "";
}

String backupDecrypt(JsonObjectConst in, const String &pass, String &plain) {
  if (String((const char *)(in["enc"] | "")) != "AES-256-GCM" || String((const char *)(in["kdf"] | "")) != "PBKDF2-SHA256")
    return "Nieznany format szyfrowania kopii";
  uint32_t iter = in["iter"] | 0;
  if (iter < 1000 || iter > 1000000) return "Nieprawidłowy plik kopii";
  if (!pass.length()) return "Podaj hasło kopii";
  uint8_t salt[16], iv[12], tag[16], key[32];
  if (unb64(in["salt"], salt, sizeof(salt)) != 16 || unb64(in["iv"], iv, sizeof(iv)) != 12 || unb64(in["tag"], tag, sizeof(tag)) != 16)
    return "Nieprawidłowy plik kopii";
  const char *d = in["data"] | "";
  size_t cap = strlen(d) * 3 / 4 + 4;
  uint8_t *ct = (uint8_t *)malloc(cap), *pt = (uint8_t *)malloc(cap + 1);
  if (!ct || !pt) { free(ct); free(pt); return "Za mało pamięci"; }
  int n = unb64(d, ct, cap);
  String err;
  if (n < 0) err = "Nieprawidłowy plik kopii";
  else if (!deriveKey(pass, salt, sizeof(salt), iter, key)) err = "Błąd wyprowadzenia klucza";
  else {
    mbedtls_gcm_context g;
    mbedtls_gcm_init(&g);
    int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (!r) r = mbedtls_gcm_auth_decrypt(&g, n, iv, sizeof(iv), (const uint8_t *)AAD, strlen(AAD), tag, sizeof(tag), ct, pt);
    mbedtls_gcm_free(&g);
    memset(key, 0, sizeof(key));
    if (r) err = "Złe hasło albo uszkodzony plik kopii";
    else { pt[n] = 0; plain = (const char *)pt; }
  }
  memset(pt, 0, cap + 1);
  free(ct); free(pt);
  return err;
}
