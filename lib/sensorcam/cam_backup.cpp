#include "cam_backup.h"
#include <string.h>
#include "cam_auth.h"
#include "cam_number.h"
#include "monocypher.h"

static const char HEAD[] = "sensorcam-backup sealed 1 ";
static const size_t HEAD_LEN = sizeof(HEAD) - 1;
static const size_t SALT = 16, NONCE = 24, TAG = 16;

bool camBackupIsSealed(const std::string& text) { return text.compare(0, HEAD_LEN, HEAD) == 0; }

bool camValidPassphrase(const std::string& p) { return p.size() >= CAM_PASSPHRASE_MIN && p.size() <= CAM_PASSPHRASE_MAX; }

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

static void derive(uint8_t key[32], const std::string& passphrase, const uint8_t salt[SALT], uint32_t blocks, uint32_t passes, void* work) {
  crypto_argon2_config config = {CRYPTO_ARGON2_ID, blocks, passes, 1};
  crypto_argon2_inputs inputs = {(const uint8_t*)passphrase.data(), salt, (uint32_t)passphrase.size(), (uint32_t)SALT};
  crypto_argon2(key, 32, work, config, inputs, crypto_argon2_no_extras);
}

bool camBackupParams(const std::string& sealed, uint32_t& blocks, uint32_t& passes) {
  if (!camBackupIsSealed(sealed)) return false;
  size_t end = sealed.find('\n');
  if (end == std::string::npos) return false;
  std::string numbers = sealed.substr(HEAD_LEN, end - HEAD_LEN);
  size_t space = numbers.find(' ');
  return space != std::string::npos && camNumberIn(numbers.substr(0, space), (uint32_t)8, CAM_BACKUP_MAX_BLOCKS, blocks) &&
         camNumberIn(numbers.substr(space + 1), (uint32_t)1, CAM_BACKUP_MAX_PASSES, passes);
}

std::string camSealBackup(const std::string& plain, const std::string& passphrase, const uint8_t salt[SALT], const uint8_t nonce[NONCE], void* work) {
  std::string head = HEAD + std::to_string(CAM_BACKUP_BLOCKS) + " " + std::to_string(CAM_BACKUP_PASSES) + "\n";
  uint8_t key[32];
  derive(key, passphrase, salt, CAM_BACKUP_BLOCKS, CAM_BACKUP_PASSES, work);
  std::string body(SALT + NONCE + TAG + plain.size(), '\0');
  uint8_t* b = (uint8_t*)&body[0];
  memcpy(b, salt, SALT);
  memcpy(b + SALT, nonce, NONCE);
  crypto_aead_lock(b + SALT + NONCE + TAG, b + SALT + NONCE, key, nonce, (const uint8_t*)head.data(), head.size(), (const uint8_t*)plain.data(),
                   plain.size());
  crypto_wipe(key, sizeof(key));
  return head + base64(b, body.size()) + "\n";
}

bool camOpenBackup(const std::string& sealed, const std::string& passphrase, std::string& plain, void* work) {
  uint32_t blocks = 0, passes = 0;
  if (!camBackupParams(sealed, blocks, passes)) return false;
  size_t end = sealed.find('\n');
  std::string head = sealed.substr(0, end + 1), rest = sealed.substr(end + 1);
  while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.pop_back();
  std::string body = camBase64Decode(rest);
  if (body.size() < SALT + NONCE + TAG) return false;
  const uint8_t* b = (const uint8_t*)body.data();
  uint8_t key[32];
  derive(key, passphrase, b, blocks, passes, work);
  std::string out(body.size() - SALT - NONCE - TAG, '\0');
  int bad = crypto_aead_unlock((uint8_t*)&out[0], b + SALT + NONCE, key, b + SALT, (const uint8_t*)head.data(), head.size(), b + SALT + NONCE + TAG,
                               out.size());
  crypto_wipe(key, sizeof(key));
  if (bad) return false;
  plain.swap(out);
  return true;
}
