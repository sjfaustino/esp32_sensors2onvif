#include "cam_pairing.h"
#include <stdio.h>
#include <string.h>
#include "cam_auth.h"
#include "cam_rtp.h"

static const char KEY_LABEL[] = "sensors2onvif node key";

bool camParseHello(const uint8_t* d, int len, uint8_t& inputs, uint8_t publicKey[CAM_X25519_LEN]) {
  if (len != CAM_HELLO_LEN || d[0] != CAM_HELLO || d[1] < 1 || d[1] > CAM_MAX_NODE_INPUTS) return false;
  inputs = d[1];
  memcpy(publicKey, d + 2, CAM_X25519_LEN);
  return true;
}

void camHelloAnswer(const uint8_t nodeMac[6], uint8_t state, uint8_t channel, const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t* nodeKey,
                    uint8_t out[CAM_ANSWER_LEN]) {
  out[0] = CAM_HELLO_ANSWER;
  out[1] = state;
  out[2] = channel;
  memcpy(out + 3, hubPublic, CAM_X25519_LEN);
  uint8_t s[20] = {0};
  if (nodeKey) camHmacSha1(nodeKey, CAM_NODE_KEY_LEN, nodeMac, 6, out, 3 + CAM_X25519_LEN, s);
  memcpy(out + 3 + CAM_X25519_LEN, s, 8);
}

bool camNodeKey(const uint8_t hubPrivate[CAM_X25519_LEN], const uint8_t hubPublic[CAM_X25519_LEN], const uint8_t nodePublic[CAM_X25519_LEN],
                uint8_t key[CAM_NODE_KEY_LEN]) {
  uint8_t shared[CAM_X25519_LEN];
  camX25519(shared, hubPrivate, nodePublic);
  uint8_t any = 0;
  for (uint8_t b : shared) any |= b;
  if (!any) return false;
  uint8_t label[sizeof(KEY_LABEL) - 1 + 2 * CAM_X25519_LEN];
  memcpy(label, KEY_LABEL, sizeof(KEY_LABEL) - 1);
  memcpy(label + sizeof(KEY_LABEL) - 1, hubPublic, CAM_X25519_LEN);
  memcpy(label + sizeof(KEY_LABEL) - 1 + CAM_X25519_LEN, nodePublic, CAM_X25519_LEN);
  camHmacSha1(shared, sizeof(shared), label, sizeof(label), nullptr, 0, key);
  memset(shared, 0, sizeof(shared));
  return true;
}

std::string camKeyFingerprint(const uint8_t publicKey[CAM_X25519_LEN]) {
  std::string hash = camSha1(std::string((const char*)publicKey, CAM_X25519_LEN));
  char out[16];
  const uint8_t* h = (const uint8_t*)hash.data();
  snprintf(out, sizeof(out), "%02X%02X-%02X%02X-%02X%02X", h[0], h[1], h[2], h[3], h[4], h[5]);
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
