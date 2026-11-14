// X25519 key agreement (RFC 7748), for pairing sensor nodes: the hub and a node each combine their own private key with
// the other's public key and arrive at the same secret, which someone listening to the radio cannot work out.
// After the public-domain TweetNaCl. It is slow, tens of milliseconds on an ESP32, and runs only when pairing.
#pragma once
#include <stdint.h>

const int CAM_X25519_LEN = 32;
void camX25519(uint8_t out[CAM_X25519_LEN], const uint8_t scalar[CAM_X25519_LEN], const uint8_t point[CAM_X25519_LEN]);
void camX25519Public(uint8_t out[CAM_X25519_LEN], const uint8_t privateKey[CAM_X25519_LEN]);
