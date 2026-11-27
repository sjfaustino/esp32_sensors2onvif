// A settings backup sealed with a passphrase the owner chooses, so the file holds no secret anyone can read: the Wi-Fi
// and hub passwords, the keys, and each paired node's key. A file of text:
//   "sensorcam-backup sealed 2 pbkdf2-sha256 ROUNDS SALT\n", SALT 16 bytes in hex, then the base64 of nonce (24 bytes),
//   tag (16) and the sealed backup, then "\n"
// The key is PBKDF2-HMAC-SHA256 of the passphrase and the salt, ROUNDS rounds, 32 bytes, worked out by the browser: so
// the slow part never runs on the hub, whose loop must keep taking alerts, and it can be slower than the hub could afford.
// The sealing is XChaCha20-Poly1305 (Monocypher), with the first line as associated data, so its numbers cannot be
// changed. Inside is the plain backup: the settings as the board writes them ("sensorcam-backup 1" and its lines), then
// a "pairing.MAC=" line for each paired node, with its public key and its key in hex, as its pairing.txt holds them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

const uint32_t CAM_BACKUP_ROUNDS = 600000;  // what the page uses, as OWASP advises for PBKDF2-HMAC-SHA256
const uint32_t CAM_BACKUP_MIN_ROUNDS = 100000, CAM_BACKUP_MAX_ROUNDS = 10000000;
const size_t CAM_BACKUP_KEY = 32, CAM_BACKUP_SALT = 16;

// A value as a plain backup's line holds it: letters, digits and .-_ as they are, every other byte as %XX, so a value
// can hold an = or a line break. Unescaping takes either, as backups before wrote every byte as %XX; false for a
// broken %XX or a byte that should have been one.
std::string camBackupEscape(const std::string& value);
bool camBackupUnescape(const std::string& text, std::string& value);

bool camBackupIsSealed(const std::string& text);
// What the browser sends: the key and salt in hex, and the rounds; false for anything else.
bool camBackupKeyFrom(const std::string& keyHex, uint8_t key[CAM_BACKUP_KEY]);
bool camBackupSaltFrom(const std::string& saltHex, uint8_t salt[CAM_BACKUP_SALT]);
bool camBackupRoundsValid(uint32_t rounds);
// nonce is random, and fresh for each file.
std::string camSealBackup(const std::string& plain, const uint8_t key[CAM_BACKUP_KEY], const uint8_t salt[CAM_BACKUP_SALT], uint32_t rounds,
                          const uint8_t nonce[24]);
// False for a wrong key or a damaged file, telling neither apart.
bool camOpenBackup(const std::string& sealed, const uint8_t key[CAM_BACKUP_KEY], std::string& plain);
