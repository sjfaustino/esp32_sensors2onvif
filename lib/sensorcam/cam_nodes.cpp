// CamNodes: pairing sensor nodes with public keys (cam_pairing.h), and their firmware updates.
//
// A node says hello with its public key on each channel until the hub answers. While pairing is open, the hub lists it
// for approval with its key's fingerprint, which the person compares with the node's label before approving. Approval
// works out the node's own key from the two public keys and the hub's private key, writes it to pairing.txt in the
// node's folder, and makes a folder for each of its inputs. From then on the hub answers the node's hello signed with
// that key, and accepts its reports only when signed with it.
#include <stdlib.h>
#include <string.h>
#include <new>
#include "cam_nodes.h"
#include "cam_server_internal.h"
#include "cam_soap.h"

void CamNodes::begin() {
  images_.load();
  pairingReady_ = camParseHex(cfg_.pairingKey, hubPrivate_, CAM_X25519_LEN);
  if (pairingReady_) camX25519Public(hubPublic_, hubPrivate_);
}

void CamNodes::hello(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  uint8_t inputs = 0, key[CAM_X25519_LEN];
  if (!pairingReady_ || !camParseHello(data, len, inputs, key)) return;
  uint32_t now = millis_();
  uint8_t channel = (uint8_t)radio_->wifiChannel();
  uint8_t answer[CAM_ANSWER_LEN];
  // A paired node asking again, to find the hub's channel: answered at any time, signed so it can trust the answer.
  int id = alarm_.findNode(mac);
  if (id && alarm_.nodeOf(id).paired && memcmp(alarm_.nodeOf(id).publicKey, key, CAM_X25519_LEN) == 0) {
    camHelloAnswer(mac, CAM_PAIR_PAIRED, channel, hubPublic_, (const uint8_t*)alarm_.nodeOf(id).key.data(), answer);
    radio_->espNowSend(mac, answer, CAM_ANSWER_LEN);
    return;
  }
  if (!pairingOpen(now)) return;  // anything else only while pairing is open, so nothing can fill the list at other times
  PairRequest* entry = nullptr;
  PairRequest* spare = &pairRequests_[0];  // a free row, or else the one heard from longest ago
  for (PairRequest& p : pairRequests_) {
    if (p.used && memcmp(p.mac, mac, 6) == 0) {
      entry = &p;
      break;
    }
    if (spare->used && (!p.used || now - p.heardAt > now - spare->heardAt)) spare = &p;
  }
  if (!entry) {
    entry = spare;
    *entry = PairRequest();
    entry->used = true;
    memcpy(entry->mac, mac, 6);
  }
  entry->inputs = inputs;
  memcpy(entry->publicKey, key, CAM_X25519_LEN);  // a node that made a new key is shown with the new one
  entry->heardAt = now;
  entry->rssi = rssi;
  camHelloAnswer(mac, CAM_PAIR_WAITING, channel, hubPublic_, nullptr, answer);
  radio_->espNowSend(mac, answer, CAM_ANSWER_LEN);
}

// Takes tens of milliseconds on the board, once per node.
bool CamNodes::approve(const uint8_t mac[6], std::string& error) {
  PairRequest* request = nullptr;
  for (PairRequest& p : pairRequests_)
    if (p.used && memcmp(p.mac, mac, 6) == 0) request = &p;
  if (!request) {
    error = "that node is not waiting for approval";
    return false;
  }
  std::string node = camMacText(mac);
  int free = CAM_MAX_SENSORS - alarm_.sensorCount(), needed = 0;
  for (int input = 1; input <= request->inputs; input++) needed += !alarm_.findSlot(mac, (uint8_t)input);
  if (needed > free) {
    error = "the hub has room for " + std::to_string(CAM_MAX_SENSORS) + " sensors, and this node's would go beyond that";
    return false;
  }
  uint8_t key[CAM_NODE_KEY_LEN];
  if (!camNodeKey(hubPrivate_, hubPublic_, request->publicKey, key)) {
    error = "that node's public key cannot be used";
    return false;
  }
  std::string text = camPairingText(request->publicKey, key);
  memset(key, 0, sizeof(key));
  bool ok = storage_.writeFile(node + "/pairing.txt", text.data(), text.size());
  for (int input = 1; ok && input <= request->inputs; input++) ok = storage_.makeDirs(node + "/" + std::to_string(input));
  alarm_.rescan();
  if (!ok) {
    error = "could not save the node; storage may be full";
    return false;
  }
  *request = PairRequest();
  return true;
}

std::string CamNodes::pairingJson() {
  uint32_t now = millis_();
  return jsonObject([&](CamJson& j) {
    j.flag("ready", pairingReady_).num("open", pairingOpen(now) ? (pairingUntil_ - now + 999) / 1000 : 0);
    j.str("hub", pairingReady_ ? camKeyFingerprint(hubPublic_) : std::string()).array("waiting");
    for (const PairRequest& p : pairRequests_) {
      if (!p.used) continue;
      int id = alarm_.findNode(p.mac);
      j.object().str("mac", camMacText(p.mac)).num("inputs", p.inputs).str("fingerprint", camKeyFingerprint(p.publicKey));
      j.num("ago", (now - p.heardAt) / 1000)
          .num("rssi", p.rssi != 0, p.rssi)
          .flag("known", id != 0)
          .flag("paired", id && alarm_.nodeOf(id).paired)
          .end();
    }
  });
}

void CamNodes::openPairing(bool open, uint32_t now) {
  pairingOpen_ = open;
  pairingUntil_ = now + CAM_PAIR_WINDOW_MS;
}

void CamNodes::forgetQuiet(uint32_t now) {
  for (PairRequest& p : pairRequests_)
    if (p.used && now - p.heardAt > CAM_PAIR_FORGET_MS) p = PairRequest();
}

void CamNodes::reject(const uint8_t mac[6]) {
  for (PairRequest& p : pairRequests_)
    if (p.used && memcmp(p.mac, mac, 6) == 0) p = PairRequest();
}

// Answered at once, without waiting for the next pass: each part is read from storage and sent as it is asked for.
void CamNodes::firmwareAsk(const uint8_t mac[6], const uint8_t* data, int len) {
  int id = alarm_.findNode(mac);
  if (!id || !radio_->hasEspNow()) return;
  CamAlarm::Node& node = alarm_.nodeOf(id);
  const std::string& key = alarm_.keyFor(&node);
  CamFirmwareAsk ask;
  CamPacketCheck check = camCheckFirmwareAsk(mac, data, len, key, ask);
  if (check == CAM_PACKET_INVALID) return;
  if (check == CAM_PACKET_BAD_SIGNATURE) return alarm_.refuseSignature(id);
  const CamNodeImage* f = images_.forChip(node.info.chip);
  if (!f) return;
  const CamNodeImage& image = *f;
  node.updating = true;
  node.updateAt = millis_();
  node.updateSize = image.size;
  uint8_t out[CAM_FIRMWARE_PART_LEN_MAX];
  if (ask.offset == CAM_FIRMWARE_DESCRIBE || ask.image != image.number) {  // a node asking for another image learns of this one
    int n = camFirmwarePart(mac, image.number, image.size, CAM_FIRMWARE_DESCRIBE, (const uint8_t*)image.version.data(), (int)image.version.size(),
                            key, out);
    radio_->espNowSend(mac, out, n);
    node.updateOffset = 0;
    return;
  }
  if (ask.offset >= image.size) return;
  node.updateOffset = ask.offset;
  uint8_t window[CAM_FIRMWARE_WINDOW * CAM_FIRMWARE_PART_MAX];
  uint32_t length = image.size - ask.offset < sizeof(window) ? image.size - ask.offset : (uint32_t)sizeof(window);
  if (!images_.read(image.chip, ask.offset, window, length)) return;
  for (uint32_t at = 0; at < length; at += CAM_FIRMWARE_PART_MAX) {
    int n = length - at < (uint32_t)CAM_FIRMWARE_PART_MAX ? (int)(length - at) : CAM_FIRMWARE_PART_MAX;
    radio_->espNowSend(mac, out, camFirmwarePart(mac, image.number, image.size, ask.offset + at, window + at, n, key, out));
  }
}
