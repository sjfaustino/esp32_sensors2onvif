// X25519 key agreement (RFC 7748), for pairing sensor nodes, from the library shared with the nodes.
#pragma once
#include "s2o_x25519.h"

const int CAM_X25519_LEN = S2O_X25519_LEN;
inline void camX25519(uint8_t out[CAM_X25519_LEN], const uint8_t scalar[CAM_X25519_LEN], const uint8_t point[CAM_X25519_LEN]) {
  s2oX25519(out, scalar, point);
}
inline void camX25519Public(uint8_t out[CAM_X25519_LEN], const uint8_t privateKey[CAM_X25519_LEN]) { s2oX25519Public(out, privateKey); }
