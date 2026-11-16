// MonitorUp - weryfikacja podpisu plikow aktualizacji (0.5.4), patrz otasig.h
#include "otasig.h"
#include "ota_pub.h"
#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>

static mbedtls_sha256_context sha;
static uint8_t tail[OTA_SIG_LEN];   // ostatnie 72 bajty (kandydat na podpis) - do skrotu dopiero, gdy przyjda kolejne
static size_t  tailN = 0;
static size_t  total = 0;

void otaSigBegin() {
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  tailN = 0; total = 0;
}

void otaSigFeed(const uint8_t *data, size_t n) {
  total += n;
  if (tailN + n <= OTA_SIG_LEN) { memcpy(tail + tailN, data, n); tailN += n; return; }
  size_t out = tailN + n - OTA_SIG_LEN;          // tyle bajtow na pewno nalezy do obrazu
  size_t fromTail = out < tailN ? out : tailN;
  mbedtls_sha256_update(&sha, tail, fromTail);
  size_t fromData = out - fromTail;
  if (fromData) mbedtls_sha256_update(&sha, data, fromData);
  // nowy ogon: reszta starego ogona + reszta danych
  size_t keepTail = tailN - fromTail;
  memmove(tail, tail + fromTail, keepTail);
  memcpy(tail + keepTail, data + fromData, n - fromData);
  tailN = keepTail + (n - fromData);
}

String otaSigFinish() {
  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);
  if (total <= OTA_SIG_LEN || tailN != OTA_SIG_LEN) return "Plik za krótki";
  if (memcmp(tail + 64, OTA_SIG_MAGIC, 8) != 0) return "Plik bez podpisu MonitorUp.pl – wgraj plik z karty wydania";

  mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if (!rc) rc = mbedtls_ecp_point_read_binary(&grp, &Q, OTA_PUBKEY, sizeof(OTA_PUBKEY));
  if (!rc) rc = mbedtls_mpi_read_binary(&r, tail, 32);
  if (!rc) rc = mbedtls_mpi_read_binary(&s, tail + 32, 32);
  if (!rc) rc = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
  mbedtls_ecp_group_free(&grp); mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
  return rc ? String("Nieprawidłowy podpis pliku – plik zmieniony albo nie pochodzi od MonitorUp.pl") : String("");
}
