// Firmware for the sensor nodes, put on the hub from the Nodes and pairing page: one file per kind of chip in nodes/, with its
// description beside it, so a start of the hub reads only the small files. The nodes fetch it over ESP-NOW, which
// CamNodes::firmwareAsk answers from here.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <functional>
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
  // Tries the chip's file on the node with this folder first, or offers it to every node at once for an empty one.
  bool setPilot(uint16_t chip, const std::string& folder);
  // The pilot runs it now: kept with the description, so a restart of the hub does not start the day again. Const, as
  // the alarm, which only reads the files otherwise, hears the node say so.
  void notePilotRan(uint16_t chip, uint32_t now) const;
  // "nodeFirmware": [{chip, name, version, size, pilot, pilotLeft, pilotHeld}]; held says why the others wait on the pilot.
  void json(CamJson& j, uint32_t now, const std::function<const char*(const CamNodeImage&)>& held) const;

  // An upload, streamed to a file beside the others as it arrives, then checked whole.
  enum Result { SAVED, NOT_AN_IMAGE, NOT_SAVED };
  bool begin();
  bool write(const char* data, size_t n);
  Result finish(bool complete, CamNodeImage& out);  // complete: every byte arrived
  void abandon();
  bool uploading() const { return upload_ != nullptr; }

 private:
  const CamStorage& storage_;
  mutable std::vector<CamNodeImage> images_;  // mutable for notePilotRan alone
  bool describe(const CamNodeImage& image) const;
  FILE* upload_ = nullptr;
};
