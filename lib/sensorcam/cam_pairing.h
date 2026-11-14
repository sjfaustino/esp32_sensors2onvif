// Pairing sensor nodes over ESP-NOW, with public keys, so a node gets a key of its own without any secret crossing the
// air. The messages are described in the README, under "Pairing a node".
//
//   hello  (node to hub, 34 bytes):  4, the number of inputs (1-16), the node's X25519 public key (32)
//   answer (hub to node, 43 bytes):  5, the state (0 waiting for approval, 1 paired), the hub's Wi-Fi channel, the hub's
//                                    public key (32), then a signature: when paired, the first 8 bytes of HMAC-SHA1
//                                    keyed with the node's key over the node's MAC address and the 35 bytes before it;
//                                    while waiting, 8 zero bytes
//
// The node's key is HMAC-SHA1, keyed with the X25519 secret the two share, over "sensors2onvif node key", the hub's
// public key and the node's public key: 20 bytes, used as the ESP-NOW key is for that node's reports and confirmations.
#pragma once
#include <stdint.h>
#include <string>
#include "cam_x25519.h"

const uint8_t CAM_HELLO = 4, CAM_HELLO_ANSWER = 5;
const int CAM_HELLO_LEN = 2 + CAM_X25519_LEN;
const int CAM_ANSWER_LEN = 3 + CAM_X25519_LEN + 8;
const int CAM_NODE_KEY_LEN = 20;
const int CAM_MAX_NODE_INPUTS = 16;
const uint8_t CAM_PAIR_WAITING = 0, CAM_PAIR_PAIRED = 1;

bool camParseHello(const uint8_t* data, int len, uint8_t& inputs, uint8_t publicKey[CAM_X25519_LEN]);
// nodeKey nullptr while waiting, which leaves the signature zero.
void camHelloAnswer(const uint8_t nodeMac[6], uint8_t state, uint8_t channel, const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t* nodeKey,
                    uint8_t out[CAM_ANSWER_LEN]);
// False when the shared secret is all zeros, which a public key chosen to be weak gives.
bool camNodeKey(const uint8_t hubPrivate[CAM_X25519_LEN], const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t nodePublic[CAM_X25519_LEN],
                uint8_t key[CAM_NODE_KEY_LEN]);
// What a person compares with the node's label: the first 6 bytes of the public key's SHA-1, as "4F2A-91C7-03BD".
std::string camKeyFingerprint(const uint8_t publicKey[CAM_X25519_LEN]);

// pairing.txt in a node's folder: "public=" and "key=" lines in lower-case hex.
std::string camPairingText(const uint8_t publicKey[CAM_X25519_LEN], const uint8_t key[CAM_NODE_KEY_LEN]);
bool camParsePairing(const std::string& text, uint8_t publicKey[CAM_X25519_LEN], uint8_t key[CAM_NODE_KEY_LEN]);
bool camParseHex(const std::string& text, uint8_t* out, size_t len);  // exactly len bytes' worth
