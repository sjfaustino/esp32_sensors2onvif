#include "cam_node_files.h"

static const char SAID[] = "said.";
static const size_t SAID_LEN = sizeof(SAID) - 1;

bool camValidName(const std::string& name) {
  if (name.size() > CAM_NAME_MAX) return false;
  for (char ch : name)
    if ((unsigned char)ch < 0x20 || ch == 0x7f) return false;
  return true;
}

// The node's own lines and what it said, each read by the parser of its part.
bool camParseNodeFile(const std::string& text, CamNodeFile& out) {
  std::string settings, said;
  camKeyLines(text, [&](const std::string& key, const std::string& value) {
    if (key.compare(0, SAID_LEN, SAID) == 0)
      said += key.substr(SAID_LEN) + "=" + value + "\n";
    else
      settings += key + "=" + value + "\n";
    return true;
  });
  CamNodeFile f;
  if (!camParseNodeSettings(settings, f.settings) || !camParseNodeInfo(said, f.info)) return false;
  out = f;
  return true;
}

std::string camNodeFileText(const CamNodeFile& f) {
  std::string out = camNodeSettingsText(f.settings);
  if (f.info.version.empty() && !f.info.everyS) return out;  // the node has said nothing yet
  std::string said = camNodeInfoText(f.info);
  camKeyLines(said, [&](const std::string& key, const std::string& value) {
    out += SAID + key + "=" + value + "\n";
    return true;
  });
  return out;
}

bool camParseSensorFile(const std::string& text, CamSensorFile& out) {
  std::string settings, name;
  bool ok = camKeyLines(text, [&](const std::string& key, const std::string& value) {
    if (key != "name") {
      settings += key + "=" + value + "\n";
      return true;
    }
    name = value;
    return camValidName(value);
  });
  CamSensorFile f;
  if (!ok || !camParseSensorSettings(settings, f.settings)) return false;
  f.name = name;
  out = f;
  return true;
}

std::string camSensorFileText(const CamSensorFile& f) {
  return (f.name.empty() ? std::string() : "name=" + f.name + "\n") + camSensorSettingsText(f.settings);
}

static bool writeOrRemove(const CamStorage& storage, const std::string& file, const std::string& text) {
  return text.empty() ? !storage.exists(file) || storage.removeTree(file) : storage.writeFile(file, text.data(), text.size());
}

bool camWriteNodeFile(const CamStorage& storage, const std::string& folder, const CamNodeFile& f) {
  return writeOrRemove(storage, folder + "/" + CAM_NODE_FILE, camNodeFileText(f));
}

bool camWriteSensorFile(const CamStorage& storage, const std::string& folder, const CamSensorFile& f) {
  return writeOrRemove(storage, folder + "/" + CAM_SENSOR_FILE, camSensorFileText(f));
}

CamNodeFile camReadNodeFile(const CamStorage& storage, const std::string& folder) {
  CamNodeFile f;
  std::string text;
  if (!storage.readFile(folder + "/" + CAM_NODE_FILE, text, 1024) || !camParseNodeFile(text, f)) f = CamNodeFile();
  // The files from before, taken in over what the one file says, as after restoring an older backup.
  bool old = false;
  CamNodeSettings settings;
  if (storage.readFile(folder + "/settings.txt", text, 256)) {
    old = true;
    if (camParseNodeSettings(text, settings)) f.settings = settings;
  }
  CamNodeInfo info;
  if (storage.readFile(folder + "/about.txt", text, 256)) {
    old = true;
    if (camParseNodeInfo(text, info)) f.info = info;
  }
  if (old && camWriteNodeFile(storage, folder, f)) {
    storage.removeTree(folder + "/settings.txt");
    storage.removeTree(folder + "/about.txt");
  }
  return f;
}

CamSensorFile camReadSensorFile(const CamStorage& storage, const std::string& folder) {
  CamSensorFile f;
  std::string text;
  if (!storage.readFile(folder + "/" + CAM_SENSOR_FILE, text, 512) || !camParseSensorFile(text, f)) f = CamSensorFile();
  bool old = false;
  if (storage.readFile(folder + "/name.txt", text, CAM_NAME_MAX)) {
    old = true;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (camValidName(text)) f.name = text;
  }
  CamSensorSettings settings;
  if (storage.readFile(folder + "/settings.txt", text, 256)) {
    old = true;
    if (camParseSensorSettings(text, settings)) f.settings = settings;
  }
  if (old && camWriteSensorFile(storage, folder, f)) {
    storage.removeTree(folder + "/name.txt");
    storage.removeTree(folder + "/settings.txt");
  }
  return f;
}
