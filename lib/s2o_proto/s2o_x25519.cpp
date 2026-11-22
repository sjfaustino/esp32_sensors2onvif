#include "s2o_x25519.h"

#include "monocypher.h"

// Monocypher's, which the firmware's signature check brings anyway, in place of the hand port of TweetNaCl these had
// been: the same RFC 7748 function, so keys made before give the same results.
void s2oX25519(uint8_t out[S2O_X25519_LEN], const uint8_t scalar[S2O_X25519_LEN], const uint8_t point[S2O_X25519_LEN]) {
  crypto_x25519(out, scalar, point);
}

void s2oX25519Public(uint8_t out[S2O_X25519_LEN], const uint8_t privateKey[S2O_X25519_LEN]) { crypto_x25519_public_key(out, privateKey); }
