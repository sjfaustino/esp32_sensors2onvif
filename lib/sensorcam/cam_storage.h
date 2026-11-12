// Picture files on LittleFS, laid out as:
//   clear.h264                      optional replacement for the built-in "all clear"
//   AA-BB-CC-DD-EE-01/              one folder per node ESP, named after its MAC address
//   AA-BB-CC-DD-EE-01/1/            one subfolder per sensor on that node
//   AA-BB-CC-DD-EE-01/1/active.h264 ... one file per state
// Uses C stdio, which reaches LittleFS through the ESP-IDF VFS and a plain folder on a PC.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_frames.h"

const size_t CAM_MAX_FRAME = 32 * 1024;  // three buffers this size are in use with two viewers, on boards without PSRAM

// CAM_PATH_TEXT is text.json, which remembers the words typed on the web page for each picture set, and a sensor's
// name.txt and settings.txt, and a node's settings.txt.
// CAM_PATH_PREVIEW is the JPEG beside a picture, made by the browser for ONVIF snapshots, because the hub cannot decode H.264.
enum CamPathKind { CAM_PATH_INVALID, CAM_PATH_CLEAR, CAM_PATH_NODE, CAM_PATH_SENSOR, CAM_PATH_FILE, CAM_PATH_TEXT, CAM_PATH_PREVIEW };
std::string camPreviewPath(const std::string& picturePath);

// Only exact, canonical names pass, which is also what keeps requests inside the storage root.
CamPathKind camClassifyPath(const std::string& path);

bool camParseMac(const std::string& text, uint8_t mac[6]);  // uppercase AA-BB-CC-DD-EE-FF
std::string camMacText(const uint8_t mac[6]);
std::string camStateFile(int direction, bool delayed);  // e.g. "approaching-delayed.h264"

// Splits an Annex-B file into out[0..2] = SPS, PPS, IDR, pointing into data.
bool camParseFrame(const uint8_t* data, size_t len, CamNal out[3]);
bool camSameNal(const CamNal& a, const CamNal& b);

// True when two baseline SPS units match in everything that affects decoding. Encoder builds can
// differ in the optional VUI section at the end, which pictures do not depend on.
bool camCompatibleSps(const CamNal& a, const CamNal& b);

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
  size_t fileSize(const std::string& path) const;  // 0 if there is no such file
  bool readFile(const std::string& path, std::string& out, size_t maxBytes) const;
  bool writeFile(const std::string& path, const char* data, size_t len) const;  // creates parent folders
  // Writes NAL units as an Annex-B file without building it in RAM first.
  bool writeNals(const std::string& path, const CamNal* parts, int count) const;
  bool makeDirs(const std::string& path) const;
  bool removeTree(const std::string& path) const;
  // Writes a file through the callback without building it in RAM first. Creates parent folders.
  bool writeWith(const std::string& path, const std::function<bool(FILE*)>& write) const;
  bool appendFile(const std::string& path, const char* data, size_t len) const;
  bool readAt(const std::string& path, size_t offset, char* out, size_t len) const;  // exactly len bytes, or false
  bool writeAt(const std::string& path, size_t offset, const char* data, size_t len) const;  // in place; makes the file if needed
  bool renameFile(const std::string& from, const std::string& to) const;  // replaces to

 private:
  std::string full(const std::string& path) const;
  std::string root_;
};
