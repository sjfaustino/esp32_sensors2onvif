// Picture files on LittleFS, laid out as:
//   clear.h264                      optional replacement for the built-in "all clear"
//   AA-BB-CC-DD-EE-01/              one folder per slave ESP, named after its MAC address
//   AA-BB-CC-DD-EE-01/1/            one subfolder per sensor on that slave
//   AA-BB-CC-DD-EE-01/1/active.h264 ... one file per state
// Uses C stdio, which reaches LittleFS through the ESP-IDF VFS and a plain folder on a PC.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "cam_frames.h"

const size_t CAM_MAX_FRAME = 8 * 1024;  // generated pictures are about 5 KB, and boards without PSRAM are short of RAM

enum CamPathKind { CAM_PATH_INVALID, CAM_PATH_CLEAR, CAM_PATH_SLAVE, CAM_PATH_SENSOR, CAM_PATH_FILE };

// Only exact, canonical names pass, which is also what keeps requests inside the storage root.
CamPathKind camClassifyPath(const std::string& path);

bool camParseMac(const std::string& text, uint8_t mac[6]);  // uppercase AA-BB-CC-DD-EE-FF
std::string camMacText(const uint8_t mac[6]);
std::string camStateFile(int direction, bool delayed);  // e.g. "approaching-delayed.h264"

// Splits an Annex-B file into out[0..2] = SPS, PPS, IDR, pointing into data.
bool camParseFrame(const uint8_t* data, size_t len, CamNal out[3]);
bool camSameNal(const CamNal& a, const CamNal& b);

struct CamFsEntry {
  std::string name;
  bool dir = false;
  uint32_t size = 0;
};

class CamStorage {
 public:
  explicit CamStorage(const std::string& root) : root_(root) {}
  bool list(const std::string& path, std::vector<CamFsEntry>& out) const;
  bool exists(const std::string& path) const;
  bool readFile(const std::string& path, std::string& out, size_t maxBytes) const;
  bool writeFile(const std::string& path, const char* data, size_t len) const;  // creates parent folders
  bool makeDirs(const std::string& path) const;
  bool removeTree(const std::string& path) const;

 private:
  std::string full(const std::string& path) const;
  std::string root_;
};
