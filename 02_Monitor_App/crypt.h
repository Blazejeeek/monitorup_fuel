// MonitorUp - szyfrowanie kopii ustawien z haslami: AES-256-GCM, klucz z PBKDF2-SHA256 (mbedTLS)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define BACKUP_KDF_ITER 10000

// plain -> out: enc, kdf, iter, salt, iv, tag, data (base64). Zwraca pusty napis albo komunikat bledu.
String backupEncrypt(const String &plain, const String &pass, JsonObject out);
// in (plik kopii) -> plain. Zle haslo lub uszkodzony plik = komunikat bledu.
String backupDecrypt(JsonObjectConst in, const String &pass, String &plain);
