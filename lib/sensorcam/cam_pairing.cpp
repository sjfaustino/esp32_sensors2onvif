#include "cam_pairing.h"
#include <stdio.h>
#include <string.h>
#include "cam_auth.h"
#include "cam_rtp.h"
#include "s2o_messages.h"

// The messages themselves are in the library shared with the nodes.
bool camParseHello(const uint8_t* d, int len, uint8_t& inputs, uint8_t publicKey[CAM_X25519_LEN]) { return s2oReadHello(d, len, inputs, publicKey); }

void camHelloAnswer(const uint8_t nodeMac[6], uint8_t state, uint8_t channel, const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t* nodeKey,
                    uint8_t out[CAM_ANSWER_LEN]) {
  s2oAnswer(nodeMac, state, channel, hubPublic, nodeKey, out);
}

bool camNodeKey(const uint8_t hubPrivate[CAM_X25519_LEN], const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t nodePublic[CAM_X25519_LEN],
                uint8_t key[CAM_NODE_KEY_LEN]) {
  return s2oNodeKey(hubPrivate, nodePublic, hubPublic, nodePublic, key);
}

std::string camKeyFingerprint(const uint8_t publicKey[CAM_X25519_LEN]) {
  char out[15];
  s2oFingerprint(publicKey, out);
  return out;
}

std::string camPairingText(const uint8_t publicKey[CAM_X25519_LEN], const uint8_t key[CAM_NODE_KEY_LEN]) {
  return "public=" + camHex(publicKey, CAM_X25519_LEN) + "\nkey=" + camHex(key, CAM_NODE_KEY_LEN) + "\n";
}

bool camParseHex(const std::string& text, uint8_t* out, size_t len) {
  if (text.size() != 2 * len) return false;
  for (size_t i = 0; i < len; i++) {
    int v = 0;
    for (int k = 0; k < 2; k++) {
      char ch = text[2 * i + k];
      int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + d;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}

bool camParsePairing(const std::string& text, uint8_t publicKey[CAM_X25519_LEN], uint8_t key[CAM_NODE_KEY_LEN]) {
  bool havePublic = false, haveKey = false;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line.compare(0, 7, "public=") == 0) havePublic = camParseHex(line.substr(7), publicKey, CAM_X25519_LEN);
    else if (line.compare(0, 4, "key=") == 0) haveKey = camParseHex(line.substr(4), key, CAM_NODE_KEY_LEN);
    else return false;
  }
  return havePublic && haveKey;
}
