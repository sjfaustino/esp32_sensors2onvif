// Firmware for the sensor nodes, put on the hub from the Nodes and pairing page: one file per kind of chip in nodes/, with its
// description beside it, so a start of the hub reads only the small files. The nodes fetch it over ESP-NOW, which
// SensorCamServer::firmwareAskPacket answers from here.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "cam_json.h"
#include "cam_sensors.h"
#include "cam_storage.h"

class CamNodeImages {
 public:
  explicit CamNodeImages(const CamStorage& storage) : storage_(storage) {}
  void load();  // the descriptions on storage; one that does not match its file is left out
  const CamNodeImage* forChip(uint16_t chip) const;
  // Exactly len bytes of the chip's file from offset.
  bool read(uint16_t chip, uint32_t offset, uint8_t* out, size_t len) const;
  bool remove(uint16_t chip);
  void json(CamJson& j) const;  // "nodeFirmware": [{chip, name, version, size}]

  // An upload, streamed to a file beside the others as it arrives, then checked whole.
  enum Result { SAVED, NOT_AN_IMAGE, NOT_SAVED };
  bool begin();
  bool write(const char* data, size_t n);
  Result finish(bool complete, CamNodeImage& out);  // complete: every byte arrived
  void abandon();
  bool uploading() const { return upload_ != nullptr; }

 private:
  const CamStorage& storage_;
  std::vector<CamNodeImage> images_;
  FILE* upload_ = nullptr;
};
