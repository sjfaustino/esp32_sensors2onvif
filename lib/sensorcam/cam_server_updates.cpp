// SensorCamServer: firmware for the sensor nodes, put on the hub from the ESP-NOW page and fetched by each node over
// ESP-NOW (see firmwareAskPacket). One file per kind of chip, in nodes/, with its description beside it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cam_server.h"
#include "cam_server_internal.h"

static const char NODE_IMAGE_UPLOAD[] = "nodes/upload.bin";

static std::string imagePath(uint16_t chip) { return "nodes/" + std::to_string(chip) + ".bin"; }
static std::string describePath(uint16_t chip) { return "nodes/" + std::to_string(chip) + ".txt"; }

// The description is written at upload, so a start of the hub need not read each file through.
void SensorCamServer::loadNodeImages() {
  nodeImages_.clear();
  std::vector<CamFsEntry> files;
  if (!storage_.list("nodes", files)) return;
  for (const CamFsEntry& e : files) {
    if (e.dir || e.name.size() < 5 || e.name.compare(e.name.size() - 4, 4, ".txt") != 0) continue;
    std::string text;
    if (!storage_.readFile("nodes/" + e.name, text, 256)) continue;
    NodeImageFile f;
    for (size_t at = 0; at < text.size();) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(at, end - at);
      at = end + 1;
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(0, eq), value = line.substr(eq + 1);
      if (key == "chip") f.image.chip = (uint16_t)atoi(value.c_str());
      else if (key == "version") f.image.version = value;
      else if (key == "number") f.image.number = (uint32_t)strtoul(value.c_str(), nullptr, 10);
      else if (key == "size") f.image.size = (uint32_t)strtoul(value.c_str(), nullptr, 10);
    }
    f.path = imagePath(f.image.chip);
    if (f.image.version.empty() || !f.image.number || storage_.fileSize(f.path) != f.image.size || imageFor(f.image.chip)) continue;
    nodeImages_.push_back(f);
  }
}

void SensorCamServer::startNodeImage(HttpConn& c, size_t length) {
  nodeImageFile_ = storage_.openWrite(NODE_IMAGE_UPLOAD);
  nodeImageOwner_ = &c;
  c.firmwareActive = true;
  c.nodeImage = true;
  c.firmwareLeft = length;
}

bool SensorCamServer::writeNodeImage(const char* data, size_t n) { return nodeImageFile_ && fwrite(data, 1, n, nodeImageFile_) == n; }

void SensorCamServer::dropNodeImage() {
  if (nodeImageFile_) fclose(nodeImageFile_);
  nodeImageFile_ = nullptr;
  nodeImageOwner_ = nullptr;
  storage_.removeTree(NODE_IMAGE_UPLOAD);
}

// Checked whole once it is on storage: it must be a node's firmware, which says its version, and not the hub's own.
void SensorCamServer::finishNodeImage(HttpConn& c, bool ok) {
  bool closed = nodeImageFile_ && fclose(nodeImageFile_) == 0;
  nodeImageFile_ = nullptr;
  nodeImageOwner_ = nullptr;
  c.firmwareActive = false;
  c.nodeImage = false;
  c.discardLeft = c.firmwareLeft;
  c.firmwareLeft = 0;
  c.answered = true;
  CamNodeImage image;
  size_t size = storage_.fileSize(NODE_IMAGE_UPLOAD);
  const CamStorage* storage = &storage_;
  bool valid = ok && closed &&
               camCheckNodeImage([storage](size_t offset, char* out, size_t len) { return storage->readAt(NODE_IMAGE_UPLOAD, offset, out, len); }, size, image);
  if (!ok || !closed) {
    storage_.removeTree(NODE_IMAGE_UPLOAD);
    c.out = jsonFail(507, "could not save the file; storage may be full");
    return;
  }
  if (!valid) {
    storage_.removeTree(NODE_IMAGE_UPLOAD);
    c.out = jsonFail(400, "that is not a sensor node's firmware: use the firmware.bin of the node project, built for the node's chip");
    return;
  }
  std::string text = "chip=" + std::to_string(image.chip) + "\nversion=" + image.version + "\nnumber=" + std::to_string(image.number) + "\nsize=" +
                     std::to_string(image.size) + "\n";
  // The description goes first: a power cut before the file is renamed then leaves a description that does not match,
  // which is ignored, rather than a file described as another.
  storage_.removeTree(describePath(image.chip));
  bool saved = storage_.renameFile(NODE_IMAGE_UPLOAD, imagePath(image.chip)) && storage_.writeFile(describePath(image.chip), text.data(), text.size());
  loadNodeImages();
  c.out = saved ? jsonOk(jsonObject([&](CamJson& j) { j.flag("ok", true).str("chip", camChipName(image.chip)).str("version", image.version); }))
                : jsonFail(507, "could not save the file; storage may be full");
}

bool SensorCamServer::apiNodeImages(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (!r.is("POST", "/api/node-firmware/delete")) return false;
  std::string chip = queryParam(r.form(), "chip");
  uint16_t n = (uint16_t)atoi(chip.c_str());
  if (chip.empty() || !imageFor(n)) {
    c.out = jsonFail(404, "there is no firmware for that chip");
    return true;
  }
  bool ok = storage_.removeTree(describePath(n)) && storage_.removeTree(imagePath(n));
  loadNodeImages();
  c.out = saved(ok, "could not delete the file");
  return true;
}

void SensorCamServer::nodeImagesJson(CamJson& j) const {
  j.array("nodeFirmware");
  for (const NodeImageFile& f : nodeImages_)
    j.object().num("chip", f.image.chip).str("name", camChipName(f.image.chip)).str("version", f.image.version).num("size", f.image.size).end();
  j.end();
}
