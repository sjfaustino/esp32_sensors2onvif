// SensorCamServer: the web side of the node firmware files that CamNodeImages keeps. The nodes fetch them over ESP-NOW
// (see firmwareAskPacket).
#include <stdlib.h>
#include <string.h>
#include "cam_server.h"
#include "cam_number.h"
#include "cam_server_internal.h"

void SensorCamServer::startNodeImage(HttpConn& c, size_t length) {
  nodeImageOwner_ = &c;
  c.firmwareActive = true;
  c.nodeImage = true;
  c.firmwareLeft = length;
}

void SensorCamServer::dropNodeImage() {
  nodeImages_.abandon();
  nodeImageOwner_ = nullptr;
}

void SensorCamServer::finishNodeImage(HttpConn& c, bool ok) {
  nodeImageOwner_ = nullptr;
  c.firmwareActive = false;
  c.nodeImage = false;
  c.discardLeft = c.firmwareLeft;
  c.firmwareLeft = 0;
  c.answered = true;
  CamNodeImage image;
  switch (nodeImages_.finish(ok, image)) {
    case CamNodeImages::SAVED:
      c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("ok", true).str("chip", camChipName(image.chip)).str("version", image.version); }));
      break;
    case CamNodeImages::NOT_AN_IMAGE:
      c.out = jsonFail(400, "that is not a sensor node's firmware: use the firmware.bin of the node project, built for the node's chip");
      break;
    default: c.out = jsonFail(507, "could not save the file; storage may be full");
  }
}

bool SensorCamServer::apiNodeImages(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (!r.is("POST", "/api/node-firmware/delete")) return false;
  std::string chip = queryParam(r.form(), "chip");
  uint16_t n = 0;
  if (!camNumber(chip, n) || !nodeImages_.forChip(n))
    c.out = jsonFail(404, "there is no firmware for that chip");
  else
    c.out = saved(nodeImages_.remove(n), "could not delete the file");
  return true;
}

// Answered at once, without waiting for the next pass: each part is read from storage and sent as it is asked for.
void SensorCamServer::firmwareAskPacket(const uint8_t mac[6], const uint8_t* data, int len) {
  int id = alarm_.findNode(mac);
  if (!id || !radio_->hasEspNow()) return;
  Node& node = alarm_.nodeOf(id);
  const std::string& key = alarm_.keyFor(&node);
  CamFirmwareAsk ask;
  CamPacketCheck check = camCheckFirmwareAsk(mac, data, len, key, ask);
  if (check == CAM_PACKET_INVALID) return;
  if (check == CAM_PACKET_BAD_SIGNATURE) return alarm_.refuseSignature(id);
  const CamNodeImage* f = imageFor(node.info.chip);
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
  if (!nodeImages_.read(image.chip, ask.offset, window, length)) return;
  for (uint32_t at = 0; at < length; at += CAM_FIRMWARE_PART_MAX) {
    int n = length - at < (uint32_t)CAM_FIRMWARE_PART_MAX ? (int)(length - at) : CAM_FIRMWARE_PART_MAX;
    radio_->espNowSend(mac, out, camFirmwarePart(mac, image.number, image.size, ask.offset + at, window + at, n, key, out));
  }
}
