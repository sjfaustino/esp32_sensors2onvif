// SensorCamServer: the web side of the node firmware files that CamNodeImages keeps. The nodes fetch them over ESP-NOW
// (see firmwareAskPacket).
#include <stdlib.h>
#include "cam_server.h"
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
  uint16_t n = (uint16_t)atoi(chip.c_str());
  if (chip.empty() || !nodeImages_.forChip(n))
    c.out = jsonFail(404, "there is no firmware for that chip");
  else
    c.out = saved(nodeImages_.remove(n), "could not delete the file");
  return true;
}
