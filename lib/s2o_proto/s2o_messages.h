// Shared by the hub and the sensor nodes (esp32_sensor_node), so both speak the same messages from one source.
// The ESP-NOW messages between them, as the hub's README describes them under "Sensor input over ESP-NOW" and
// "Pairing a node". No heap and no Arduino, so the same code runs on both boards and in the PC tests.
//
// Every signed message ends in S2O_SIG_LEN bytes: the first 8 of HMAC-SHA1, keyed with the signing key (a paired node's
// own 20-byte key, or the hub's ESP-NOW key as text), over the node's 6-byte MAC address and then the message before it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "s2o_sha1.h"
#include "s2o_x25519.h"

const uint8_t S2O_REPORT = 2, S2O_CONFIRMATION = 3, S2O_HELLO = 4, S2O_ANSWER = 5, S2O_INFO = 6, S2O_SEALED = 7, S2O_FIRMWARE_ASK = 8,
              S2O_FIRMWARE_PART = 9;
const int S2O_SIG_LEN = 8;
const int S2O_NODE_KEY_LEN = 20;
const uint32_t S2O_AGE_UNKNOWN = 0xFFFFFFFFu;
const uint8_t S2O_BATTERY_UNKNOWN = 0xFF;
const uint16_t S2O_DISTANCE_UNKNOWN = 0xFFFF, S2O_RESPONSE_UNKNOWN = 0xFFFF, S2O_CHIP_UNKNOWN = 0xFFFF;
const int S2O_MAX_TARGETS = 3, S2O_DIRECTIONS = 5;
const int S2O_TEXT_MAX = 24;
const int S2O_MAX_INPUTS = 16;

// The key a message is signed with.
struct S2oKey {
  const uint8_t* data;
  size_t len;
};

// The signature of len bytes, sent by or to the node with this MAC address.
void s2oSign(S2oKey key, const uint8_t mac[6], const uint8_t* data, int len, uint8_t out[S2O_SIG_LEN]);
// Signs the len bytes at message in place: their signature follows them. Returns the length with it.
int s2oAddSignature(S2oKey key, const uint8_t mac[6], uint8_t* message, int len);
// Whether the last S2O_SIG_LEN bytes of the len at message are its signature. Every byte is compared, so the time
// taken does not reveal how much matched. An empty key signs nothing.
bool s2oSignatureOk(S2oKey key, const uint8_t mac[6], const uint8_t* message, int len);

// ---- reports, node to hub ----

struct S2oTarget {
  uint16_t distanceCm = S2O_DISTANCE_UNKNOWN;
  uint8_t direction = 0;  // 0 unknown, 1 approaching, 2 moving away, 3 left to right, 4 right to left
};
// 2, input, active, battery, counter (u32), age (u32), fresh (u32), target count, then 3 bytes a target, then the
// signature. fresh is the number from the hub's last confirmation, 0 when the node has none: it shows the report was
// made now, not recorded earlier and played back.
struct S2oReport {
  uint8_t input = 1;  // 1 to 255
  bool active = false;
  uint8_t battery = S2O_BATTERY_UNKNOWN;  // percent; above 100 is unknown
  uint32_t counter = 0;  // a new number for each new report; the same again for a resend
  uint32_t age = 0;      // seconds since it happened; 0 when live
  uint32_t fresh = 0;
  uint8_t targetCount = 0;
  S2oTarget targets[S2O_MAX_TARGETS];
};
const int S2O_REPORT_HEAD = 17;
const int S2O_REPORT_MAX = S2O_REPORT_HEAD + 3 * S2O_MAX_TARGETS + S2O_SIG_LEN;
int s2oReportBody(const S2oReport& r, uint8_t* out);  // its length, without the signature
// body is the message without its signature.
bool s2oReadReport(const uint8_t* body, int len, S2oReport& out);

// ---- confirmations, hub to node ----

// 3, the report's input, active and counter, then what the hub has for the node: its time (seconds since 1970 UTC, 0
// while not set), the report time set on the hub (u32 seconds, 0 for the node's own), the repeat time (u16 seconds, 0
// likewise), the end of a walk test (u32, hub time, 0 for none), flags (1: an update is ready; 2: prove the node is
// there, by reporting again with this fresh number), and the fresh number for its next report. Then the signature.
struct S2oForNode {
  uint32_t hubTime = 0;
  uint32_t everyS = 0;
  uint16_t repeatS = 0;
  uint32_t walkUntil = 0;
  bool update = false;
  bool prove = false;
  uint32_t fresh = 0;
};
const int S2O_CONFIRMATION_BODY = 26;
const int S2O_CONFIRMATION_LEN = S2O_CONFIRMATION_BODY + S2O_SIG_LEN;
void s2oConfirmationBody(const S2oReport& r, const S2oForNode& forNode, uint8_t out[S2O_CONFIRMATION_BODY]);
// True when body is a confirmation of exactly this report; forNode gets what came with it.
bool s2oReadConfirmation(const uint8_t* body, int len, const S2oReport& r, S2oForNode& forNode);

// ---- node info, node to hub ----

// 6, report time (u32), flags (1: saving its battery), why it last restarted, restarts (u16), how long the last alert
// took in ms (u16), chip (u16), the update it refused (u32), then its sensor's kind and firmware version, each as a
// length and printable text of up to S2O_TEXT_MAX. Then the signature. Not confirmed.
enum S2oRestart { S2O_RESTART_UNKNOWN, S2O_RESTART_POWER, S2O_RESTART_BUTTON, S2O_RESTART_SOFTWARE, S2O_RESTART_CRASH, S2O_RESTART_WATCHDOG,
                  S2O_RESTART_BROWNOUT, S2O_RESTART_KINDS };
struct S2oInfo {
  uint32_t everyS = 0;
  bool saving = false;
  uint8_t restart = S2O_RESTART_UNKNOWN;
  uint16_t restarts = 0;
  uint16_t responseMs = S2O_RESPONSE_UNKNOWN;
  uint16_t chip = S2O_CHIP_UNKNOWN;
  uint32_t refused = 0;
  char sensor[S2O_TEXT_MAX + 1] = "";
  char version[S2O_TEXT_MAX + 1] = "";
};
const int S2O_INFO_HEAD = 17;
const int S2O_INFO_MAX = S2O_INFO_HEAD + 2 * (1 + S2O_TEXT_MAX) + S2O_SIG_LEN;
// Copies text into a field, cut to S2O_TEXT_MAX, with anything not printable ASCII made '?', so the rest still arrives.
void s2oSetText(char out[S2O_TEXT_MAX + 1], const char* text);
int s2oInfoBody(const S2oInfo& info, uint8_t* out);
bool s2oReadInfo(const uint8_t* body, int len, S2oInfo& out);  // false for a text that is not printable ASCII

// ---- pairing ----

// Hello, node to hub: 4, the number of inputs (1 to 16), the node's X25519 public key. Answer, hub to node: 5, the state
// (0 waiting for approval, 1 paired), the hub's Wi-Fi channel, the hub's public key, then when paired the signature with
// the node's key, while waiting 8 zeros.
const int S2O_HELLO_LEN = 2 + S2O_X25519_LEN;
const int S2O_ANSWER_BODY = 3 + S2O_X25519_LEN;
const int S2O_ANSWER_LEN = S2O_ANSWER_BODY + S2O_SIG_LEN;
const uint8_t S2O_WAITING = 0, S2O_PAIRED = 1;
void s2oHello(uint8_t inputs, const uint8_t nodePublic[S2O_X25519_LEN], uint8_t out[S2O_HELLO_LEN]);
bool s2oReadHello(const uint8_t* data, int len, uint8_t& inputs, uint8_t nodePublic[S2O_X25519_LEN]);
void s2oAnswer(const uint8_t nodeMac[6], uint8_t state, uint8_t channel, const uint8_t hubPublic[S2O_X25519_LEN], const uint8_t* nodeKey,
               uint8_t out[S2O_ANSWER_LEN]);
// The signature, when paired, is the caller's to check, with the key it works out from the hub's public key.
bool s2oReadAnswer(const uint8_t* data, int len, uint8_t& state, uint8_t& channel, uint8_t hubPublic[S2O_X25519_LEN]);
// The node's key: HMAC-SHA1, keyed with the X25519 secret of own private key and the other's public key, over
// "sensors2onvif node key", the hub's public key and the node's. False when that secret is all zeros, as a public key
// chosen to be weak gives.
bool s2oNodeKey(const uint8_t ownPrivate[S2O_X25519_LEN], const uint8_t otherPublic[S2O_X25519_LEN], const uint8_t hubPublic[S2O_X25519_LEN],
                const uint8_t nodePublic[S2O_X25519_LEN], uint8_t key[S2O_NODE_KEY_LEN]);
// What a person compares with the node's label: the first 6 bytes of the public key's SHA-1, as "4F2A-91C7-03BD".
void s2oFingerprint(const uint8_t publicKey[S2O_X25519_LEN], char out[15]);

// ---- sealed messages ----

// 7, a number its sender never uses twice with one key, the message inside without its signature XORed with a key
// stream, then the signature over all of that. Block i of the stream, 20 bytes, is HMAC-SHA1 keyed with the seal key
// over the direction (0 to the hub, 1 from it), the number and i. The seal key is HMAC-SHA1, keyed with the signing key,
// over "sensors2onvif seal".
const int S2O_SEALED_EXTRA = 5;
const int S2O_SEALED_MAX_INNER = 120;
// Its length, S2O_SEALED_EXTRA + innerLen + S2O_SIG_LEN.
int s2oSeal(S2oKey key, const uint8_t nodeMac[6], uint32_t number, const uint8_t* inner, int innerLen, bool fromHub, uint8_t* out);
// The message inside, without a signature, into out: its length, 0 for something that is not a sealed message or does
// not fit, -1 for a wrong signature.
int s2oOpen(S2oKey key, const uint8_t nodeMac[6], const uint8_t* data, int len, bool fromHub, uint8_t* out, int outMax);

// ---- firmware updates ----

// Ask, node to hub: 8, the update's number (0 when not known yet), the offset or S2O_DESCRIBE, signature. Part, hub to
// node: 9, the number, the size, the offset, up to S2O_PART_MAX bytes of the file or, for a description, its version,
// signature. The hub answers an ask with up to S2O_WINDOW parts.
const uint32_t S2O_DESCRIBE = 0xFFFFFFFFu;
const int S2O_ASK_LEN = 9 + S2O_SIG_LEN;
const int S2O_PART_HEAD = 13;
const int S2O_PART_MAX = 200;
const int S2O_PART_LEN_MAX = S2O_PART_HEAD + S2O_PART_MAX + S2O_SIG_LEN;
const int S2O_WINDOW = 4;
void s2oAsk(S2oKey key, const uint8_t nodeMac[6], uint32_t image, uint32_t offset, uint8_t out[S2O_ASK_LEN]);
bool s2oReadAsk(const uint8_t* data, int len, uint32_t& image, uint32_t& offset);  // the signature is the caller's to check
int s2oPart(S2oKey key, const uint8_t nodeMac[6], uint32_t image, uint32_t size, uint32_t offset, const uint8_t* part, int partLen,
            uint8_t out[S2O_PART_LEN_MAX]);
struct S2oPart {
  uint32_t image = 0, size = 0, offset = 0;
  const uint8_t* data = nullptr;  // into the message
  int len = 0;
};
bool s2oReadPart(const uint8_t* data, int len, S2oPart& out);  // likewise

// The node's firmware holds this text followed by its version, which is how the hub learns the version of a file.
#define S2O_NODE_IMAGE_MARK "sensors2onvif-node-version="
