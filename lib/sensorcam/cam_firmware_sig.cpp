#include "cam_firmware_sig.h"

#include <string.h>

bool CamFirmwareSig::begin(size_t total) {
  started_ = total > CAM_FW_TRAILER;
  imageLeft_ = started_ ? total - CAM_FW_TRAILER : 0;
  trailerGot_ = 0;
  crypto_sha512_init(&hash_);
  return started_;
}

size_t CamFirmwareSig::take(const uint8_t* data, size_t n) {
  if (!started_) return 0;
  size_t image = n < imageLeft_ ? n : imageLeft_;
  if (image) crypto_sha512_update(&hash_, data, image);
  imageLeft_ -= image;
  size_t rest = n - image;
  size_t room = CAM_FW_TRAILER - trailerGot_;
  if (rest > room) {  // more than the size given at the start: never good
    started_ = false;
    return image;
  }
  memcpy(trailer_ + trailerGot_, data + image, rest);
  trailerGot_ += rest;
  return image;
}

bool CamFirmwareSig::good(const uint8_t publicKey[32]) {
  if (!started_ || imageLeft_ || trailerGot_ != CAM_FW_TRAILER) return false;
  started_ = false;  // checked once
  if (memcmp(trailer_ + CAM_FW_SIG_LEN, CAM_FW_MAGIC, 8) != 0) return false;
  uint8_t message[16 + 64];
  memcpy(message, CAM_FW_SIG_CONTEXT, 16);
  crypto_sha512_final(&hash_, message + 16);
  return crypto_ed25519_check(trailer_, publicKey, message, sizeof(message)) == 0;
}

bool camFirmwareKey(const std::string& hex, uint8_t out[32]) {
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
