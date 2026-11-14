// SensorCamServer: pairing sensor nodes with public keys (cam_pairing.h).
//
// A node says hello with its public key on each channel until the hub answers. While pairing is open, the hub lists it
// for approval with its key's fingerprint, which the person compares with the node's label before approving. Approval
// works out the node's own key from the two public keys and the hub's private key, writes it to pairing.txt in the
// node's folder, and makes a folder for each of its inputs. From then on the hub answers the node's hello signed with
// that key, and accepts its reports only when signed with it.
#include <stdlib.h>
#include <string.h>
#include <new>
#include "cam_server.h"
#include "cam_server_internal.h"
#include "cam_soap.h"

int SensorCamServer::findNode(const uint8_t mac[6]) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

void SensorCamServer::helloPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  uint8_t inputs = 0, key[CAM_X25519_LEN];
  if (!pairingReady_ || !camParseHello(data, len, inputs, key)) return;
  uint32_t now = millis_();
  uint8_t channel = (uint8_t)(cfg_.wifiChannel ? cfg_.wifiChannel() : 0);
  uint8_t answer[CAM_ANSWER_LEN];
  // A paired node asking again, to find the hub's channel: answered at any time, signed so it can trust the answer.
  int id = findNode(mac);
  if (id && slots_[id - 1].paired && memcmp(slots_[id - 1].nodePublic, key, CAM_X25519_LEN) == 0) {
    camHelloAnswer(mac, CAM_PAIR_PAIRED, channel, hubPublic_, (const uint8_t*)slots_[id - 1].key.data(), answer);
    camCall(cfg_.espNowSend, mac, answer, CAM_ANSWER_LEN);
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
  camCall(cfg_.espNowSend, mac, answer, CAM_ANSWER_LEN);
}

// Writes the node's key, then makes its inputs' folders. Takes tens of milliseconds on the board, once per node.
bool SensorCamServer::approveNode(const uint8_t mac[6], std::string& error) {
  PairRequest* request = nullptr;
  for (PairRequest& p : pairRequests_)
    if (p.used && memcmp(p.mac, mac, 6) == 0) request = &p;
  if (!request) {
    error = "that node is not waiting for approval";
    return false;
  }
  std::string node = camMacText(mac);
  int free = 0, needed = 0;
  for (const Slot& s : slots_) free += !s.used;
  for (int input = 1; input <= request->inputs; input++) needed += !findSlot(mac, (uint8_t)input);
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
  rescan();
  if (!ok) {
    error = "could not save the node; storage may be full";
    return false;
  }
  *request = PairRequest();
  return true;
}

std::string SensorCamServer::pairingJson() {
  uint32_t now = millis_();
  return jsonObject([&](CamJson& j) {
    j.flag("ready", pairingReady_).num("open", pairingOpen(now) ? (pairingUntil_ - now + 999) / 1000 : 0);
    j.str("hub", pairingReady_ ? camKeyFingerprint(hubPublic_) : std::string()).array("waiting");
    for (const PairRequest& p : pairRequests_) {
      if (!p.used) continue;
      int id = findNode(p.mac);
      j.object().str("mac", camMacText(p.mac)).num("inputs", p.inputs).str("fingerprint", camKeyFingerprint(p.publicKey));
      j.num("ago", (now - p.heardAt) / 1000).num("rssi", p.rssi != 0, p.rssi).flag("known", id != 0).flag("paired", id && slots_[id - 1].paired).end();
    }
  });
}

bool SensorCamServer::apiPairing(const ApiRequest& r) {
  HttpConn& c = r.c;
  uint32_t now = millis_();
  for (PairRequest& p : pairRequests_)  // forgotten once they stop asking
    if (p.used && now - p.heardAt > CAM_PAIR_FORGET_MS) p = PairRequest();
  if (r.is("GET", "/api/pairing")) {
    reply(c, 200, JSON_TYPE, pairingJson());
  } else if (r.is("POST", "/api/pairing/open") || r.is("POST", "/api/pairing/close")) {
    if (!pairingReady_) {
      c.out = jsonFail(409, "pairing is not available: the hub has no pairing key");
      return true;
    }
    pairingOpen_ = r.route == "/api/pairing/open";
    pairingUntil_ = now + CAM_PAIR_WINDOW_MS;
    c.out = jsonOk(JSON_OK);
  } else if (r.is("POST", "/api/pairing/approve") || r.is("POST", "/api/pairing/reject")) {
    uint8_t mac[6];
    if (!camParseMac(queryParam(r.form(), "mac"), mac)) {
      c.out = jsonFail(400, "give the node's MAC address, as AA-BB-CC-DD-EE-FF");
      return true;
    }
    std::string error;
    if (r.route == "/api/pairing/reject") {
      for (PairRequest& p : pairRequests_)
        if (p.used && memcmp(p.mac, mac, 6) == 0) p = PairRequest();
      c.out = jsonOk(JSON_OK);
    } else {
      c.out = approveNode(mac, error) ? jsonOk(JSON_OK) : jsonFail(409, error);
    }
  } else {
    return false;
  }
  return true;
}
