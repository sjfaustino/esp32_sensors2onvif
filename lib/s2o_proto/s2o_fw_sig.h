// The signature on a firmware update, for the hub and for the sensor nodes. The hub keeps a firmware sent to it over the
// network only when it is signed with the project's key, so its password alone cannot put other firmware on it; and a
// node checks the signature on its own update before it runs it, so neither can a hub that is not the project's. A
// cable to the board still can.
//
// A signed file is the firmware image as built, then a trailer of S2O_FW_TRAILER bytes:
//   the Ed25519 signature (64), then the 8 bytes "S2OSIG1\n".
// The signature is over a context of 16 bytes, followed by the SHA-512 of the image (64), so the image can be hashed as it
// arrives and checked once its last byte is in: S2O_FW_SIG_CONTEXT for the hub's own firmware and S2O_FW_NODE_CONTEXT for
// a node's, so neither passes for the other. tools/sign_firmware.py makes it, with --node for a node's.
// A hub or node from before signing takes such a file too: the trailer is written after the image, where nothing reads it.
//
// The signed part ends with the release's number, so a hub refuses a firmware older than its own, which may have faults
// since fixed, and a node likewise one older than its own: S2O_FW_RELEASE_LEN bytes, the number as 4 bytes, least significant first, then the 8 bytes
// "S2OREL1\n". It is signed with the image, and a hub that does not look for it writes it after the image with the rest. The number is major *
// 1000000 + minor * 1000 + patch of the release tag the build comes from (tools/version.py).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "monocypher-ed25519.h"

static const size_t S2O_FW_SIG_LEN = 64;
static const size_t S2O_FW_TRAILER = S2O_FW_SIG_LEN + 8;
static const char S2O_FW_MAGIC[] = "S2OSIG1\n";                 // 8 bytes, without the terminating zero
static const char S2O_FW_SIG_CONTEXT[] = "s2o firmware v1\n";   // the hub's: 16 bytes, without the terminating zero
static const char S2O_FW_NODE_CONTEXT[] = "s2o nodefirm v1\n";  // a node's
static const size_t S2O_FW_RELEASE_LEN = 12;
static const char S2O_FW_RELEASE_MAGIC[] = "S2OREL1\n";  // 8 bytes, without the terminating zero

// Takes a signed file in pieces of any size, says which bytes of each are the image, and checks the signature at the end.
class S2oFirmwareSig {
 public:
  // total is the size of the whole file; false when it is too short to hold a trailer.
  bool begin(size_t total);
  // Takes the next n bytes and returns how many of them, from the start, belong to the image; the rest are the trailer's.
  size_t take(const uint8_t* data, size_t n);
  // Whether the whole file has been taken, its trailer is well formed, and the signature is good for this 32-byte key,
  // as the hub's firmware or, with S2O_FW_NODE_CONTEXT, a node's.
  bool good(const uint8_t publicKey[32], const char* context = S2O_FW_SIG_CONTEXT);
  // The release number at the end of the signed part, once all of it is taken; -1 when it has none. Trust it only once
  // good() has said the signature is right.
  int64_t release() const;

 private:
  crypto_sha512_ctx hash_;
  size_t imageLeft_ = 0;
  size_t trailerGot_ = 0;
  bool started_ = false;
  uint8_t trailer_[S2O_FW_TRAILER];
  uint8_t tail_[S2O_FW_RELEASE_LEN] = {};  // the last bytes of the signed part, where the release number is
  size_t tailGot_ = 0;
};

// The 32 bytes of a key written as 64 hex digits; false for anything else.
bool s2oFirmwareKey(const std::string& hex, uint8_t out[32]);
