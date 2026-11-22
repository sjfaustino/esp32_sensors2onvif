// CamWeb: the web side of CamNodes. Pairing, and the node firmware files, which the nodes fetch over ESP-NOW
// (see CamNodes::firmwareAsk).
#include <stdlib.h>
#include <string.h>
#include "cam_web_server.h"
#include "cam_number.h"
#include "cam_server_internal.h"

void CamWeb::startNodeImage(HttpConn& c, size_t length) {
  nodeImageOwner_ = &c;
  c.firmwareActive = true;
  c.nodeImage = true;
  c.firmwareLeft = length;
  nodeSig_.begin(length);
}

void CamWeb::dropNodeImage() {
  nodes_.images().abandon();
  nodeImageOwner_ = nullptr;
}

void CamWeb::finishNodeImage(HttpConn& c, bool ok) {
  nodeImageOwner_ = nullptr;
  c.firmwareActive = false;
  c.nodeImage = false;
  c.discardLeft = c.firmwareLeft;
  c.drainFrom = millis_();
  c.firmwareLeft = 0;
  c.answered = true;
  CamNodeImage image;
  // Signed with the project's key, as a node's firmware: the nodes check it again, but one that would fail there is
  // never offered to them.
  uint8_t key[32];
  bool signedOk = ok && s2oFirmwareKey(cfg_.firmwareKey, key) && nodeSig_.good(key, S2O_FW_NODE_CONTEXT);
  if (ok && !signedOk) {
    nodes_.images().finish(false, image);
    c.out = jsonFail(400, "that node firmware is not signed for these nodes, so nothing was changed: use a signed firmware.bin of the node project");
    return;
  }
  switch (nodes_.images().finish(ok, image)) {
    case CamNodeImages::SAVED:
      c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("ok", true).str("chip", camChipName(image.chip)).str("version", image.version); }));
      break;
    case CamNodeImages::NOT_AN_IMAGE:
      c.out = jsonFail(400, "that is not a sensor node's firmware: use the firmware.bin of the node project, built for the node's chip");
      break;
    default: c.out = jsonFail(507, "could not save the file; storage may be full");
  }
}

void CamWeb::postNodeFirmwareDelete(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string chip = r.field("chip");
  uint16_t n = 0;
  if (!camNumber(chip, n) || !nodes_.images().forChip(n))
    c.out = jsonFail(404, "there is no firmware for that chip");
  else
    c.out = saved(nodes_.images().remove(n), "could not delete the file");
}

void CamWeb::getPairing(const ApiRequest& r) {
  HttpConn& c = r.c;
  uint32_t now = millis_();
  nodes_.forgetQuiet(now);
  reply(c, 200, JSON_TYPE, nodes_.pairingJson());
}

void CamWeb::pairingOpenOrClose(const ApiRequest& r) {
  HttpConn& c = r.c;
  uint32_t now = millis_();
  nodes_.forgetQuiet(now);
  if (!nodes_.pairingReady()) {
    c.out = jsonFail(409, "pairing is not available: the hub has no pairing key");
    return;
  }
  nodes_.openPairing(r.route == "/api/pairing/open", now);
  c.out = jsonOk(JSON_OK);
}

void CamWeb::pairingDecide(const ApiRequest& r) {
  HttpConn& c = r.c;
  uint32_t now = millis_();
  nodes_.forgetQuiet(now);
  uint8_t mac[6];
  if (!camParseMac(r.field("mac"), mac)) {
    c.out = jsonFail(400, "give the node's MAC address, as AA-BB-CC-DD-EE-FF");
    return;
  }
  std::string error;
  if (r.route == "/api/pairing/reject") {
    nodes_.reject(mac);
    c.out = jsonOk(JSON_OK);
  } else {
    c.out = nodes_.approve(mac, r.field("fingerprint"), error) ? jsonOk(JSON_OK) : jsonFail(409, error);
  }
}
