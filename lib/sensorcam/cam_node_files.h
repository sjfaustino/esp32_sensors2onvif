// What the hub keeps for each node and each sensor, one file each, in lines of "key=value" (camKeyLines):
//   node.txt in the node's folder: its settings from the pages (late, silent, every, repeat, hw; CamNodeSettings), and
//     what the node last said about itself, each key after "said." (CamNodeInfo)
//   sensor.txt in the sensor's folder: "name=" its name, and its settings (hold, quiet, zone, camera; CamSensorSettings)
// Kept apart: pairing.txt, the node's key, which the pages never show; battery.txt, a log that only grows; and each
// sensor's text.json, the words of its pictures, which the page writes as a whole.
//
// Before, these were settings.txt and about.txt in the node's folder, and name.txt and settings.txt in each sensor's.
// Reading a folder that still has them, as after a restore of an older backup, takes them in, writes the one file, and
// removes them.
#pragma once
#include <string>
#include "cam_sensors.h"
#include "cam_storage.h"

const char CAM_NODE_FILE[] = "node.txt";
const char CAM_SENSOR_FILE[] = "sensor.txt";
const size_t CAM_NAME_MAX = 64;

struct CamNodeFile {
  CamNodeSettings settings;
  CamNodeInfo info;
};
struct CamSensorFile {
  std::string name;
  CamSensorSettings settings;
};

bool camParseNodeFile(const std::string& text, CamNodeFile& out);  // false for a line it does not take
std::string camNodeFileText(const CamNodeFile& f);
bool camParseSensorFile(const std::string& text, CamSensorFile& out);
std::string camSensorFileText(const CamSensorFile& f);
// A name on one line, of printable text, up to CAM_NAME_MAX bytes; empty for none.
bool camValidName(const std::string& name);

// From storage, in folder: a missing or damaged file reads as the defaults, as if nothing had been set or said.
CamNodeFile camReadNodeFile(const CamStorage& storage, const std::string& folder);
CamSensorFile camReadSensorFile(const CamStorage& storage, const std::string& folder);
// Written whole; a file with nothing in it is removed instead.
bool camWriteNodeFile(const CamStorage& storage, const std::string& folder, const CamNodeFile& f);
bool camWriteSensorFile(const CamStorage& storage, const std::string& folder, const CamSensorFile& f);
