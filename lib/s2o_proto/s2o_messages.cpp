#include "s2o_messages.h"
#include "monocypher.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static bool sameBytes(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t differ = 0;
  for (size_t i = 0; i < n; i++) differ |= a[i] ^ b[i];
  return differ == 0;
}

// ---- signatures ----

void s2oSign(S2oKey key, const uint8_t mac[6], const uint8_t* data, int len, uint8_t out[S2O_SIG_LEN]) {
  uint8_t full[20];
  s2oHmacSha1(key.data, key.len, mac, 6, data, (size_t)len, full);
  memcpy(out, full, S2O_SIG_LEN);
}

int s2oAddSignature(S2oKey key, const uint8_t mac[6], uint8_t* message, int len) {
  s2oSign(key, mac, message, len, message + len);
  return len + S2O_SIG_LEN;
}

bool s2oSignatureOk(S2oKey key, const uint8_t mac[6], const uint8_t* message, int len) {
  if (len < S2O_SIG_LEN || !key.len) return false;
  uint8_t expected[S2O_SIG_LEN];
  s2oSign(key, mac, message, len - S2O_SIG_LEN, expected);
  return sameBytes(expected, message + len - S2O_SIG_LEN, S2O_SIG_LEN);
}

// ---- reports ----

int s2oReportBody(const S2oReport& r, uint8_t* out) {
  out[0] = S2O_REPORT;
  out[1] = r.input;
  out[2] = r.active ? 1 : 0;
  out[3] = r.battery;
  put32(out + 4, r.counter);
  put32(out + 8, r.age);
  put32(out + 12, r.fresh);
  int count = r.targetCount <= S2O_MAX_TARGETS ? r.targetCount : S2O_MAX_TARGETS;
  out[16] = (uint8_t)count;
  for (int i = 0; i < count; i++) {
    uint8_t* t = out + S2O_REPORT_HEAD + 3 * i;
    put16(t, r.targets[i].distanceCm);
    t[2] = r.targets[i].direction;
  }
  return S2O_REPORT_HEAD + 3 * count;
}

bool s2oReadReport(const uint8_t* d, int len, S2oReport& out) {
  if (len < S2O_REPORT_HEAD || d[0] != S2O_REPORT || d[1] == 0 || d[2] > 1 || d[16] > S2O_MAX_TARGETS) return false;
  if (len != S2O_REPORT_HEAD + 3 * d[16]) return false;
  S2oReport r;
  r.input = d[1];
  r.active = d[2] == 1;
  r.battery = d[3] <= 100 ? d[3] : S2O_BATTERY_UNKNOWN;
  r.counter = get32(d + 4);
  r.age = get32(d + 8);
  r.fresh = get32(d + 12);
  r.targetCount = d[16];
  for (int i = 0; i < r.targetCount; i++) {
    const uint8_t* t = d + S2O_REPORT_HEAD + 3 * i;
    if (t[2] >= S2O_DIRECTIONS) return false;
    r.targets[i].distanceCm = get16(t);
    r.targets[i].direction = t[2];
  }
  out = r;
  return true;
}

// ---- confirmations ----

void s2oConfirmationBody(const S2oReport& r, const S2oForNode& e, uint8_t d[S2O_CONFIRMATION_BODY]) {
  d[0] = S2O_CONFIRMATION;
  d[1] = r.input;
  d[2] = r.active ? 1 : 0;
  put32(d + 3, r.counter);
  put32(d + 7, e.hubTime);
  put32(d + 11, e.everyS);
  put16(d + 15, e.repeatS);
  put32(d + 17, e.walkUntil);
  d[21] = (uint8_t)((e.update ? 1 : 0) | (e.prove ? 2 : 0) | (e.hardware ? 4 : 0));
  put32(d + 22, e.fresh);
}

bool s2oReadConfirmation(const uint8_t* d, int len, const S2oReport& r, S2oForNode& e) {
  if (len != S2O_CONFIRMATION_BODY || d[0] != S2O_CONFIRMATION || d[1] != r.input || d[2] != (r.active ? 1 : 0) || get32(d + 3) != r.counter)
    return false;
  e.hubTime = get32(d + 7);
  e.everyS = get32(d + 11);
  e.repeatS = get16(d + 15);
  e.walkUntil = get32(d + 17);
  e.update = (d[21] & 1) != 0;
  e.prove = (d[21] & 2) != 0;
  e.hardware = (d[21] & 4) != 0;
  e.fresh = get32(d + 22);
  return true;
}

// ---- the node's hardware ----

static int putHardware(uint8_t* out, const S2oHardware& hw) {
  put32(out, hw.number);
  out[4] = hw.sensor;
  out[5] = hw.sensorPin;
  out[6] = hw.tamperPin;
  out[7] = hw.batteryPin;
  out[8] = hw.cell;
  return S2O_HARDWARE_FIELDS;
}

static bool getHardware(const uint8_t* d, S2oHardware& out) {
  if (d[4] < S2O_SENSOR_GENERIC_HIGH || d[4] >= S2O_SENSOR_KINDS || d[8] >= S2O_CELL_KINDS) return false;
  out.number = get32(d);
  out.sensor = d[4];
  out.sensorPin = d[5];
  out.tamperPin = d[6];
  out.batteryPin = d[7];
  out.cell = d[8];
  return true;
}

void s2oHardwareAsk(S2oKey key, const uint8_t nodeMac[6], uint8_t out[S2O_HARDWARE_ASK_LEN]) {
  out[0] = S2O_HARDWARE_ASK;
  s2oAddSignature(key, nodeMac, out, 1);
}

void s2oHardwareMessage(S2oKey key, const uint8_t nodeMac[6], const S2oHardware& hw, uint8_t out[S2O_HARDWARE_LEN]) {
  out[0] = S2O_HARDWARE;
  putHardware(out + 1, hw);
  s2oAddSignature(key, nodeMac, out, S2O_HARDWARE_BODY);
}

bool s2oReadHardware(const uint8_t* d, int len, S2oHardware& out) {
  return len == S2O_HARDWARE_BODY && d[0] == S2O_HARDWARE && getHardware(d + 1, out);
}

bool s2oPinWakes(uint16_t chip, uint8_t pin) {
  switch (chip) {
    case S2O_CHIP_ESP32C6: return pin <= 7;
    case S2O_CHIP_ESP32C3: return pin <= 5 && pin != 2;
    case S2O_CHIP_ESP32S3: return pin >= 1 && pin <= 21;
    case S2O_CHIP_ESP32: return pin == 2 || pin == 4 || (pin >= 12 && pin <= 15) || (pin >= 25 && pin <= 27) || (pin >= 32 && pin <= 39);
    default: return pin < 49;
  }
}

bool s2oPinReadsBattery(uint16_t chip, uint8_t pin) {
  switch (chip) {
    case S2O_CHIP_ESP32C6: return pin <= 6;
    case S2O_CHIP_ESP32C3: return pin <= 4;
    case S2O_CHIP_ESP32S3: return pin >= 1 && pin <= 10;
    case S2O_CHIP_ESP32: return pin >= 32 && pin <= 39;
    default: return pin < 49;
  }
}

bool s2oHardwareValid(const S2oHardware& hw, uint16_t chip, const char** why) {
  const char* fault = nullptr;
  bool tamper = hw.tamperPin != S2O_PIN_NONE, battery = hw.batteryPin != S2O_PIN_NONE;
  if (hw.sensor < S2O_SENSOR_GENERIC_HIGH || hw.sensor >= S2O_SENSOR_KINDS || hw.cell >= S2O_CELL_KINDS)
    fault = "no such sensor or battery";
  else if (hw.sensorPin == S2O_PIN_NONE || !s2oPinWakes(chip, hw.sensorPin))
    fault = "the sensor's pin cannot wake this chip";
  else if (tamper && !s2oPinWakes(chip, hw.tamperPin))
    fault = "the tamper switch's pin cannot wake this chip";
  else if (battery && !s2oPinReadsBattery(chip, hw.batteryPin))
    fault = "the battery's pin cannot read a voltage on this chip";
  else if (battery != (hw.cell != S2O_CELL_NONE))
    fault = "a battery needs both its pin and its kind";
  else if ((tamper && hw.tamperPin == hw.sensorPin) || (battery && (hw.batteryPin == hw.sensorPin || hw.batteryPin == hw.tamperPin)))
    fault = "a pin is used twice";
  else if (chip == S2O_CHIP_ESP32 && tamper && hw.sensor == S2O_SENSOR_GENERIC_LOW)
    fault = "a classic ESP32 with a tamper switch needs a sensor whose output goes high";
  if (why) *why = fault;
  return !fault;
}

// ---- node info ----

void s2oSetText(char out[S2O_TEXT_MAX + 1], const char* text) {
  int n = 0;
  for (; text && text[n] && n < S2O_TEXT_MAX; n++) out[n] = text[n] >= 0x20 && text[n] <= 0x7E ? text[n] : '?';
  out[n] = 0;
}

static int putText(uint8_t* out, const char* text) {
  int n = (int)strnlen(text, S2O_TEXT_MAX);
  out[0] = (uint8_t)n;
  for (int i = 0; i < n; i++) out[1 + i] = text[i] >= 0x20 && text[i] <= 0x7E ? (uint8_t)text[i] : '?';
  return 1 + n;
}

int s2oInfoBody(const S2oInfo& info, uint8_t* out) {
  out[0] = S2O_INFO;
  put32(out + 1, info.everyS);
  out[5] = info.saving ? 1 : 0;
  out[6] = info.restart;
  put16(out + 7, info.restarts);
  put16(out + 9, info.responseMs);
  put16(out + 11, info.chip);
  put32(out + 13, info.refused);
  int len = S2O_INFO_HEAD;
  len += putText(out + len, info.sensor);
  len += putText(out + len, info.version);
  if (info.hasHardware) len += putHardware(out + len, info.hardware);
  if (info.hasHardware && info.hasFresh) {
    put32(out + len, info.fresh);
    len += 4;
  }
  return len;
}

bool s2oReadInfo(const uint8_t* d, int len, S2oInfo& out) {
  if (len < S2O_INFO_HEAD + 2 || d[0] != S2O_INFO || d[5] > 1 || d[6] >= S2O_RESTART_KINDS) return false;
  S2oInfo info;
  info.everyS = get32(d + 1);
  info.saving = d[5] == 1;
  info.restart = d[6];
  info.restarts = get16(d + 7);
  info.responseMs = get16(d + 9);
  info.chip = get16(d + 11);
  info.refused = get32(d + 13);
  int at = S2O_INFO_HEAD;
  for (char* text : {info.sensor, info.version}) {
    if (at >= len || d[at] > S2O_TEXT_MAX || at + 1 + d[at] > len) return false;
    for (int i = 0; i < d[at]; i++) {
      uint8_t ch = d[at + 1 + i];
      if (ch < 0x20 || ch > 0x7E) return false;
      text[i] = (char)ch;
    }
    text[d[at]] = 0;
    at += 1 + d[at];
  }
  // A node that says its hardware, and perhaps the hub's number after it; one from before says nothing more.
  if (at + S2O_HARDWARE_FIELDS == len || at + S2O_HARDWARE_FIELDS + 4 == len) {
    if (!getHardware(d + at, info.hardware)) return false;
    info.hasHardware = true;
    at += S2O_HARDWARE_FIELDS;
    if (at + 4 == len) {
      info.hasFresh = true;
      info.fresh = get32(d + at);
      at += 4;
    }
  }
  if (at != len) return false;
  out = info;
  return true;
}

// ---- pairing ----

static const char KEY_LABEL[] = "sensors2onvif node key";

void s2oHello(uint8_t inputs, const uint8_t nodePublic[S2O_X25519_LEN], uint8_t out[S2O_HELLO_LEN]) {
  out[0] = S2O_HELLO;
  out[1] = inputs;
  memcpy(out + 2, nodePublic, S2O_X25519_LEN);
}

bool s2oReadHello(const uint8_t* d, int len, uint8_t& inputs, uint8_t nodePublic[S2O_X25519_LEN]) {
  if (len != S2O_HELLO_LEN || d[0] != S2O_HELLO || d[1] < 1 || d[1] > S2O_MAX_INPUTS) return false;
  inputs = d[1];
  memcpy(nodePublic, d + 2, S2O_X25519_LEN);
  return true;
}

void s2oAnswer(const uint8_t nodeMac[6], uint8_t state, uint8_t channel, const uint8_t hubPublic[S2O_X25519_LEN], const uint8_t* nodeKey,
               uint8_t out[S2O_ANSWER_LEN]) {
  out[0] = S2O_ANSWER;
  out[1] = state;
  out[2] = channel;
  memcpy(out + 3, hubPublic, S2O_X25519_LEN);
  if (nodeKey)
    s2oAddSignature({nodeKey, S2O_NODE_KEY_LEN}, nodeMac, out, S2O_ANSWER_BODY);
  else
    memset(out + S2O_ANSWER_BODY, 0, S2O_SIG_LEN);
}

bool s2oReadAnswer(const uint8_t* d, int len, uint8_t& state, uint8_t& channel, uint8_t hubPublic[S2O_X25519_LEN]) {
  if (len != S2O_ANSWER_LEN || d[0] != S2O_ANSWER || d[1] > S2O_PAIRED) return false;
  state = d[1];
  channel = d[2];
  memcpy(hubPublic, d + 3, S2O_X25519_LEN);
  return true;
}

bool s2oNodeKey(const uint8_t ownPrivate[S2O_X25519_LEN], const uint8_t otherPublic[S2O_X25519_LEN], const uint8_t hubPublic[S2O_X25519_LEN],
                const uint8_t nodePublic[S2O_X25519_LEN], uint8_t key[S2O_NODE_KEY_LEN]) {
  uint8_t shared[S2O_X25519_LEN];
  s2oX25519(shared, ownPrivate, otherPublic);
  uint8_t any = 0;
  for (uint8_t b : shared) any |= b;
  if (!any) return false;
  uint8_t label[sizeof(KEY_LABEL) - 1 + 2 * S2O_X25519_LEN];
  memcpy(label, KEY_LABEL, sizeof(KEY_LABEL) - 1);
  memcpy(label + sizeof(KEY_LABEL) - 1, hubPublic, S2O_X25519_LEN);
  memcpy(label + sizeof(KEY_LABEL) - 1 + S2O_X25519_LEN, nodePublic, S2O_X25519_LEN);
  s2oHmacSha1(shared, sizeof(shared), label, sizeof(label), nullptr, 0, key);
  memset(shared, 0, sizeof(shared));
  return true;
}

void s2oFingerprint(const uint8_t publicKey[S2O_X25519_LEN], char out[15]) {
  S2oSha1 s;
  s.add(publicKey, S2O_X25519_LEN);
  uint8_t h[20];
  s.finish(h);
  snprintf(out, 15, "%02X%02X-%02X%02X-%02X%02X", h[0], h[1], h[2], h[3], h[4], h[5]);
}

void s2oFingerprintLong(const uint8_t publicKey[S2O_X25519_LEN], char out[20]) {
  S2oSha1 s;
  s.add(publicKey, S2O_X25519_LEN);
  uint8_t h[20];
  s.finish(h);
  snprintf(out, 20, "%02X%02X-%02X%02X-%02X%02X-%02X%02X", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}

// ---- sealed messages ----

static const char SEAL_LABEL[] = "sensors2onvif seal";
static const char AEAD_LABEL[] = "sensors2onvif aead v1";

// The old form's key stream: block i, 20 bytes, is HMAC-SHA1 keyed with the seal key over the direction, number and i.
static void sealStream(S2oKey key, bool fromHub, uint32_t number, uint8_t* data, int len) {
  uint8_t sealKey[20];
  s2oHmacSha1(key.data, key.len, (const uint8_t*)SEAL_LABEL, sizeof(SEAL_LABEL) - 1, nullptr, 0, sealKey);
  for (int block = 0; block * 20 < len; block++) {
    uint8_t in[6] = {(uint8_t)(fromHub ? 1 : 0), 0, 0, 0, 0, (uint8_t)block}, stream[20];
    put32(in + 1, number);
    s2oHmacSha1(sealKey, sizeof(sealKey), in, sizeof(in), nullptr, 0, stream);
    for (int i = 0; i < 20 && block * 20 + i < len; i++) data[block * 20 + i] ^= stream[i];
  }
  crypto_wipe(sealKey, sizeof(sealKey));
}

// The new form's key, nonce and associated data: the node's MAC address and the head.
static void aeadInputs(S2oKey key, const uint8_t nodeMac[6], const uint8_t head[S2O_SEALED_HEAD], bool fromHub, uint8_t aeadKey[32],
                       uint8_t nonce[24], uint8_t ad[6 + S2O_SEALED_HEAD]) {
  crypto_blake2b_keyed(aeadKey, 32, key.data, key.len, (const uint8_t*)AEAD_LABEL, sizeof(AEAD_LABEL) - 1);
  memset(nonce, 0, 24);
  nonce[0] = fromHub ? 1 : 0;
  memcpy(nonce + 1, head + 1, 4);
  memcpy(ad, nodeMac, 6);
  memcpy(ad + 6, head, S2O_SEALED_HEAD);
}

int s2oSeal(S2oKey key, const uint8_t nodeMac[6], uint32_t number, const uint8_t* inner, int innerLen, bool fromHub, uint8_t* out) {
  out[0] = S2O_SEALED_AEAD;
  put32(out + 1, number);
  uint8_t aeadKey[32], nonce[24], ad[6 + S2O_SEALED_HEAD];
  aeadInputs(key, nodeMac, out, fromHub, aeadKey, nonce, ad);
  crypto_aead_lock(out + S2O_SEALED_HEAD, out + S2O_SEALED_HEAD + innerLen, aeadKey, nonce, ad, sizeof(ad), inner, innerLen);
  crypto_wipe(aeadKey, sizeof(aeadKey));
  return S2O_SEALED_HEAD + innerLen + S2O_SEALED_TAG;
}

int s2oSealOld(S2oKey key, const uint8_t nodeMac[6], uint32_t number, const uint8_t* inner, int innerLen, bool fromHub, uint8_t* out) {
  out[0] = S2O_SEALED;
  put32(out + 1, number);
  memcpy(out + S2O_SEALED_HEAD, inner, innerLen);
  sealStream(key, fromHub, number, out + S2O_SEALED_HEAD, innerLen);
  return s2oAddSignature(key, nodeMac, out, S2O_SEALED_HEAD + innerLen);
}

int s2oOpen(S2oKey key, const uint8_t nodeMac[6], const uint8_t* d, int len, bool fromHub, uint8_t* out, int outMax, bool* old) {
  if (len < 1 || (d[0] != S2O_SEALED_AEAD && d[0] != S2O_SEALED)) return 0;
  bool isOld = d[0] == S2O_SEALED;
  if (old) *old = isOld;
  int inner = len - S2O_SEALED_HEAD - (isOld ? S2O_SIG_LEN : S2O_SEALED_TAG);
  if (inner < 1 || inner > S2O_SEALED_MAX_INNER || inner > outMax) return 0;
  if (isOld) {
    if (!s2oSignatureOk(key, nodeMac, d, len)) return -1;
    memcpy(out, d + S2O_SEALED_HEAD, inner);
    sealStream(key, fromHub, get32(d + 1), out, inner);
    return inner;
  }
  uint8_t aeadKey[32], nonce[24], ad[6 + S2O_SEALED_HEAD];
  aeadInputs(key, nodeMac, d, fromHub, aeadKey, nonce, ad);
  int bad = crypto_aead_unlock(out, d + S2O_SEALED_HEAD + inner, aeadKey, nonce, ad, sizeof(ad), d + S2O_SEALED_HEAD, inner);
  crypto_wipe(aeadKey, sizeof(aeadKey));
  return bad ? -1 : inner;
}

// ---- firmware updates ----

void s2oAsk(S2oKey key, const uint8_t nodeMac[6], uint32_t image, uint32_t offset, uint8_t out[S2O_ASK_LEN]) {
  out[0] = S2O_FIRMWARE_ASK;
  put32(out + 1, image);
  put32(out + 5, offset);
  s2oAddSignature(key, nodeMac, out, 9);
}

bool s2oReadAsk(const uint8_t* d, int len, uint32_t& image, uint32_t& offset) {
  if (len != S2O_ASK_LEN || d[0] != S2O_FIRMWARE_ASK) return false;
  image = get32(d + 1);
  offset = get32(d + 5);
  return true;
}

int s2oPart(S2oKey key, const uint8_t nodeMac[6], uint32_t image, uint32_t size, uint32_t offset, const uint8_t* part, int partLen,
            uint8_t out[S2O_PART_LEN_MAX]) {
  if (partLen > S2O_PART_MAX) partLen = S2O_PART_MAX;
  out[0] = S2O_FIRMWARE_PART;
  put32(out + 1, image);
  put32(out + 5, size);
  put32(out + 9, offset);
  memcpy(out + S2O_PART_HEAD, part, partLen);
  return s2oAddSignature(key, nodeMac, out, S2O_PART_HEAD + partLen);
}

bool s2oReadPart(const uint8_t* d, int len, S2oPart& out) {
  int n = len - S2O_PART_HEAD - S2O_SIG_LEN;
  if (n < 0 || n > S2O_PART_MAX || d[0] != S2O_FIRMWARE_PART) return false;
  out.image = get32(d + 1);
  out.size = get32(d + 5);
  out.offset = get32(d + 9);
  out.data = d + S2O_PART_HEAD;
  out.len = n;
  return true;
}
