#include "s2o_fw_sig.h"

#include <string.h>

bool S2oFirmwareSig::begin(size_t total) {
  started_ = total > S2O_FW_TRAILER;
  imageLeft_ = started_ ? total - S2O_FW_TRAILER : 0;
  trailerGot_ = 0;
  tailGot_ = 0;
  crypto_sha512_init(&hash_);
  return started_;
}

int64_t S2oFirmwareSig::release() const {
  if (imageLeft_ || tailGot_ < S2O_FW_RELEASE_LEN || memcmp(tail_ + 4, S2O_FW_RELEASE_MAGIC, 8) != 0) return -1;
  return (int64_t)tail_[0] | (int64_t)tail_[1] << 8 | (int64_t)tail_[2] << 16 | (int64_t)tail_[3] << 24;
}

size_t S2oFirmwareSig::take(const uint8_t* data, size_t n) {
  if (!started_) return 0;
  size_t image = n < imageLeft_ ? n : imageLeft_;
  if (image) crypto_sha512_update(&hash_, data, image);
  imageLeft_ -= image;
  // The last S2O_FW_RELEASE_LEN bytes hashed so far, oldest first.
  if (image >= S2O_FW_RELEASE_LEN) {
    memcpy(tail_, data + image - S2O_FW_RELEASE_LEN, S2O_FW_RELEASE_LEN);
    tailGot_ = S2O_FW_RELEASE_LEN;
  } else if (image) {
    size_t keep = tailGot_ + image > S2O_FW_RELEASE_LEN ? S2O_FW_RELEASE_LEN - image : tailGot_;
    memmove(tail_, tail_ + tailGot_ - keep, keep);
    memcpy(tail_ + keep, data, image);
    tailGot_ = keep + image;
  }
  size_t rest = n - image;
  size_t room = S2O_FW_TRAILER - trailerGot_;
  if (rest > room) {  // more than the size given at the start: never good
    started_ = false;
    return image;
  }
  memcpy(trailer_ + trailerGot_, data + image, rest);
  trailerGot_ += rest;
  return image;
}

bool S2oFirmwareSig::good(const uint8_t publicKey[32], const char* context) {
  if (!started_ || imageLeft_ || trailerGot_ != S2O_FW_TRAILER) return false;
  started_ = false;  // checked once
  if (memcmp(trailer_ + S2O_FW_SIG_LEN, S2O_FW_MAGIC, 8) != 0) return false;
  uint8_t message[16 + 64];
  memcpy(message, context, 16);
  crypto_sha512_final(&hash_, message + 16);
  return crypto_ed25519_check(trailer_, publicKey, message, sizeof(message)) == 0;
}

bool s2oFirmwareKey(const std::string& hex, uint8_t out[32]) {
  if (hex.size() != 64) return false;
  for (size_t i = 0; i < 32; i++) {
    int v = 0;
    for (size_t k = 0; k < 2; k++) {
      char c = hex[i * 2 + k];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + d;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}
