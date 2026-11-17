// CamSensorTable: the sensor and node rows, read from the folders on storage.
#include "cam_sensor_table.h"
#include <string.h>
#include "cam_number.h"
#include "cam_storage.h"

CamSensorTable::CamSensorTable(const CamConfig& cfg, CamStorage& storage)
    : cfg_(cfg), storage_(storage), slots_(CAM_MAX_SENSORS), nodes_(CAM_MAX_SENSORS), health_(CAM_MAX_SENSORS) {}

int CamSensorTable::findNode(const uint8_t mac[6]) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

int CamSensorTable::findSlot(const uint8_t mac[6], uint8_t input) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && slots_[i].input == input && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

// The node's own setting, or else worked out from how often the node says it reports, or else the hub's default.
uint32_t CamSensorTable::silentMsFor(int id) const {
  const Node& n = nodeOf(id);
  uint32_t auto_ = camAutoSilentS(n.info.everyS);
  return n.settings.silentS ? n.settings.silentS * 1000 : auto_ ? auto_ * 1000 : cfg_.silentMs;
}

// The input number in a path such as AA-BB-CC-DD-EE-01/2 or AA-BB-CC-DD-EE-01/2/file, slash being the first '/'.
static bool pathInput(const std::string& path, size_t slash, uint8_t& input) {
  return camNumber(path.substr(slash + 1, path.find('/', slash + 1) - slash - 1), input);
}

int CamSensorTable::slotForPath(const std::string& path) const {
  uint8_t mac[6], input = 0;
  size_t slash = path.find('/');
  if (camClassifyPath(path) != CAM_PATH_SENSOR || !camParseMac(path.substr(0, slash), mac) || !pathInput(path, slash, input)) return 0;
  return findSlot(mac, input);
}

std::string CamSensorTable::slotName(int id) const { return camMacText(slotOf(id).mac) + "/" + std::to_string(slotOf(id).input); }

bool CamSensorTable::sensorFits(const std::string& path) const {
  uint8_t mac[6];
  size_t slash = path.find('/');
  if (slash == std::string::npos || !camParseMac(path.substr(0, slash), mac)) return true;  // not inside a sensor's folder
  uint8_t input = 0;
  if (pathInput(path, slash, input) && findSlot(mac, input)) return true;
  for (const Slot& s : slots_)
    if (!s.used) return true;
  return false;
}

// What Home Assistant is told of the sensors: which there are, and their names.
uint32_t CamSensorTable::sensorsTag() const {
  uint32_t h = 2166136261u;  // FNV-1a
  auto add = [&](const void* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 16777619u;
  };
  for (const Slot& s : slots_) {
    add(&s.used, 1);
    add(s.mac, 6);
    add(&s.input, 1);
    add(s.name.data(), s.name.size() + 1);
  }
  return h;
}

CamSensorTable::Node* CamSensorTable::nodeByMac(const uint8_t mac[6]) {
  for (Node& n : nodes_)
    if (n.used && memcmp(n.mac, mac, 6) == 0) return &n;
  return nullptr;
}

int CamSensorTable::sensorCount() const {
  int n = 0;
  for (const Slot& s : slots_) n += s.used;
  return n;
}

void CamSensorTable::noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi, uint32_t now) {
  UnknownSender* entry = nullptr;
  UnknownSender* spare = &unknown_[0];  // a free row, or else the one silent for longest
  for (UnknownSender& u : unknown_) {
    if (u.used && u.input == input && memcmp(u.mac, mac, 6) == 0) {
      entry = &u;
      break;
    }
    if (spare->used && (!u.used || now - u.heardAt > now - spare->heardAt)) spare = &u;
  }
  if (!entry) {
    entry = spare;
    *entry = UnknownSender();
    entry->used = true;
    memcpy(entry->mac, mac, 6);
    entry->input = input;
  }
  entry->rssi = rssi;
  entry->heardAt = now;
  entry->messages++;
}

CamSensorTable::Changes CamSensorTable::rescan(uint32_t now) {
  Changes changes;
  uint32_t before = sensorsTag();
  bool seen[CAM_MAX_SENSORS] = {false}, nodeSeen[CAM_MAX_SENSORS] = {false};
  // A folder that cannot be read changes nothing: forgetting its sensors would ignore their alerts as coming from
  // unknown senders until the next change. A deleted folder is still noticed, as it is simply not listed.
  std::vector<CamFsEntry> folders;
  if (!storage_.list("", folders)) return changes;
  changes.readable = true;
  for (const CamFsEntry& s : folders) {
    uint8_t mac[6];
    if (!s.dir || !camParseMac(s.name, mac)) continue;
    // The node's entry, kept across rescans for what is only in memory; or a free one.
    Node* node = nodeByMac(mac);
    for (int i = 0; !node && i < CAM_MAX_SENSORS; i++) {
      if (nodes_[i].used) continue;
      nodes_[i] = Node();
      nodes_[i].used = true;
      memcpy(nodes_[i].mac, mac, 6);
      node = &nodes_[i];
    }
    if (!node) continue;  // more nodes than there can be sensors: none of its sensors could have a place
    int index = (int)(node - nodes_.data());
    nodeSeen[index] = true;
    std::vector<CamFsEntry> inputs;
    if (!storage_.list(s.name, inputs)) {
      for (int i = 0; i < CAM_MAX_SENSORS; i++) seen[i] = seen[i] || (slots_[i].used && memcmp(slots_[i].mac, mac, 6) == 0);
      continue;
    }
    std::string text;
    CamNodeSettings settings;  // a damaged file means the defaults
    if (!storage_.readFile(s.name + "/settings.txt", text, 256) || !camParseNodeSettings(text, settings)) settings = CamNodeSettings();
    node->settings = settings;
    CamNodeInfo info;  // likewise: a damaged file is as if the node had said nothing
    if (!storage_.readFile(s.name + "/about.txt", text, 256) || !camParseNodeInfo(text, info)) info = CamNodeInfo();
    info.responseMs = node->info.responseMs;  // only in memory
    node->info = info;
    std::vector<CamBatteryPoint> battery;  // and a damaged battery log is no forecast
    if (!storage_.readFile(s.name + "/battery.txt", text, 8192) || !camParseBatteryLog(text, battery)) battery.clear();
    node->batteryAt = battery.empty() ? 0 : battery.back().t;
    node->daysLeft = camBatteryDaysLeft(battery);
    // A damaged pairing file leaves the node unpaired, so its reports are checked with the shared key and refused: it
    // shows on the Nodes and pairing page as refused, rather than being trusted with a key that may not be its own.
    uint8_t nodeKey[CAM_NODE_KEY_LEN];
    node->paired = storage_.readFile(s.name + "/pairing.txt", text, 256) && camParsePairing(text, node->publicKey, nodeKey);
    node->key = node->paired ? std::string((const char*)nodeKey, CAM_NODE_KEY_LEN) : std::string();
    for (const CamFsEntry& e : inputs) {
      if (!e.dir || camClassifyPath(s.name + "/" + e.name) != CAM_PATH_SENSOR) continue;
      uint8_t input = 0;
      camNumber(e.name, input);  // a sensor folder's name is always 1 to 255
      int id = findSlot(mac, input);
      for (int i = 0; id == 0 && i < CAM_MAX_SENSORS; i++) {
        if (slots_[i].used) continue;
        slots_[i].used = true;
        memcpy(slots_[i].mac, mac, 6);
        slots_[i].input = input;
        health_[i] = Health();
        health_[i].watchedAt = now;
        changes.fresh[i] = true;
        id = i + 1;
      }
      if (!id) continue;
      seen[id - 1] = true;
      std::string folder = s.name + "/" + e.name, name;
      slotOf(id).name = storage_.readFile(folder + "/name.txt", name, 64) ? name : std::string();
      CamSensorSettings sensor;  // a damaged file means the defaults
      if (!storage_.readFile(folder + "/settings.txt", text, 256) || !camParseSensorSettings(text, sensor)) sensor = CamSensorSettings();
      slotOf(id).settings = sensor;
      slotOf(id).node = index + 1;
    }
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    if (nodes_[i].used && !nodeSeen[i]) nodes_[i] = Node();
    if (!slots_[i].used || seen[i]) continue;
    slots_[i] = Slot();
    health_[i] = Health();
    changes.gone[i] = true;
    changes.fresh[i] = true;
  }
  changes.listChanged = sensorsTag() != before;
  return changes;
}
