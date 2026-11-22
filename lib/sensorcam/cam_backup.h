// A settings backup sealed with a passphrase the owner chooses, so the file holds no secret anyone can read: the Wi-Fi
// and hub passwords, the keys, and each paired node's key. A file of text:
//   "sensorcam-backup sealed 1 BLOCKS PASSES\n", then the base64 of salt (16 bytes), nonce (24), tag (16) and the sealed
//   backup, then "\n"
// The key is Argon2id of the passphrase and the salt, with BLOCKS KiB of memory and PASSES passes; the sealing is
// XChaCha20-Poly1305 (Monocypher), with the first line as associated data, so its numbers cannot be changed.
// Inside is the plain backup: the settings as the board writes them ("sensorcam-backup 1" and its lines), then a
// "pairing.MAC=" line for each paired node, with its public key and its key in hex, as its pairing.txt holds them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

const uint32_t CAM_BACKUP_BLOCKS = 32;      // KiB: what an ESP32 can spare beside the video, for a moment
const uint32_t CAM_BACKUP_PASSES = 48;      // passes over that memory, so each guess at the passphrase costs time
const uint32_t CAM_BACKUP_MAX_BLOCKS = 64;  // what a file may ask for when opened
const uint32_t CAM_BACKUP_MAX_PASSES = 400;
const size_t CAM_PASSPHRASE_MIN = 8, CAM_PASSPHRASE_MAX = 128;

bool camBackupIsSealed(const std::string& text);
bool camValidPassphrase(const std::string& passphrase);
// The memory and passes a sealed file names, within the limits above; false for a file that is not one.
bool camBackupParams(const std::string& sealed, uint32_t& blocks, uint32_t& passes);
// work holds CAM_BACKUP_BLOCKS KiB; salt and nonce are random, and fresh for each file.
std::string camSealBackup(const std::string& plain, const std::string& passphrase, const uint8_t salt[16], const uint8_t nonce[24], void* work);
// work holds the KiB camBackupParams said. False for a wrong passphrase or a damaged file, telling neither apart.
bool camOpenBackup(const std::string& sealed, const std::string& passphrase, std::string& plain, void* work);
