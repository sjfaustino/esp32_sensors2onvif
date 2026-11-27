#include "cam_backup.h"
#include <stdio.h>
#include <string.h>
#include "cam_auth.h"
#include "cam_number.h"
#include "monocypher.h"

static const char HEAD[] = "sensorcam-backup sealed 2 pbkdf2-sha256 ";
static const size_t HEAD_LEN = sizeof(HEAD) - 1;
static const size_t NONCE = 24, TAG = 16;

bool camBackupIsSealed(const std::string& text) {
  static const char SEALED[] = "sensorcam-backup sealed ";
  return text.compare(0, sizeof(SEALED) - 1, SEALED) == 0;
}

static bool hexInto(const std::string& text, uint8_t* out, size_t n) {
  if (text.size() != 2 * n) return false;
  for (size_t i = 0; i < 2 * n; i++) {
    char ch = text[i];
    int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
    if (digit < 0) return false;
    out[i / 2] = (uint8_t)(i % 2 ? out[i / 2] << 4 | digit : digit);
  }
  return true;
}

bool camBackupKeyFrom(const std::string& keyHex, uint8_t key[CAM_BACKUP_KEY]) { return hexInto(keyHex, key, CAM_BACKUP_KEY); }
bool camBackupSaltFrom(const std::string& saltHex, uint8_t salt[CAM_BACKUP_SALT]) { return hexInto(saltHex, salt, CAM_BACKUP_SALT); }
bool camBackupRoundsValid(uint32_t rounds) { return rounds >= CAM_BACKUP_MIN_ROUNDS && rounds <= CAM_BACKUP_MAX_ROUNDS; }

static std::string base64(const uint8_t* d, size_t n) {
  static const char ABC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    out += ABC[v >> 18 & 63];
    out += ABC[v >> 12 & 63];
    out += i + 1 < n ? ABC[v >> 6 & 63] : '=';
    out += i + 2 < n ? ABC[v & 63] : '=';
  }
  return out;
}

std::string camSealBackup(const std::string& plain, const uint8_t key[CAM_BACKUP_KEY], const uint8_t salt[CAM_BACKUP_SALT], uint32_t rounds,
                          const uint8_t nonce[NONCE]) {
  char saltHex[2 * CAM_BACKUP_SALT + 1];
  for (size_t i = 0; i < CAM_BACKUP_SALT; i++) snprintf(saltHex + 2 * i, 3, "%02x", salt[i]);
  std::string head = HEAD + std::to_string(rounds) + " " + saltHex + "\n";
  std::string body(NONCE + TAG + plain.size(), '\0');
  uint8_t* b = (uint8_t*)&body[0];
  memcpy(b, nonce, NONCE);
  crypto_aead_lock(b + NONCE + TAG, b + NONCE, key, nonce, (const uint8_t*)head.data(), head.size(), (const uint8_t*)plain.data(), plain.size());
  return head + base64(b, body.size()) + "\n";
}

bool camOpenBackup(const std::string& sealed, const uint8_t key[CAM_BACKUP_KEY], std::string& plain) {
  size_t end = sealed.find('\n');
  if (sealed.compare(0, HEAD_LEN, HEAD) != 0 || end == std::string::npos) return false;
  std::string head = sealed.substr(0, end + 1), rest = sealed.substr(end + 1);
  while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.pop_back();
  std::string body = camBase64Decode(rest);
  if (body.size() < NONCE + TAG) return false;
  const uint8_t* b = (const uint8_t*)body.data();
  std::string out(body.size() - NONCE - TAG, '\0');
  if (crypto_aead_unlock((uint8_t*)&out[0], b + NONCE, key, b, (const uint8_t*)head.data(), head.size(), b + NONCE + TAG, out.size())) return false;
  plain.swap(out);
  return true;
}
