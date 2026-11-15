#include "cam_sensors.h"
#include <stdlib.h>
#include <string.h>
#include "cam_auth.h"
#include "s2o_messages.h"

static const uint32_t CYCLE_MS = 2000;
static uint16_t readU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// The messages are in the library shared with the nodes; these keep the hub's names and its std::string keys.
static S2oKey keyOf(const std::string& key) { return {(const uint8_t*)key.data(), key.size()}; }

static CamPacketCheck checkSignature(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key) {
  return s2oSignatureOk(keyOf(key), mac, d, len) ? CAM_PACKET_SIGNED : CAM_PACKET_BAD_SIGNATURE;
}

bool parseSensorPacket(const uint8_t* d, int len, SensorReport& out) {
  return len > CAM_SIGNATURE_LEN && s2oReadReport(d, len - CAM_SIGNATURE_LEN, out);
}

std::string camPacketSignature(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key) {
  uint8_t s[CAM_SIGNATURE_LEN];
  s2oSign(keyOf(key), mac, d, len, s);
  return std::string((const char*)s, CAM_SIGNATURE_LEN);
}

CamPacketCheck camCheckPacket(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, SensorReport& out) {
  return parseSensorPacket(d, len, out) ? checkSignature(mac, d, len, key) : CAM_PACKET_INVALID;
}

static bool printable(const std::string& t) {
  for (char ch : t)
    if (ch < 0x20 || ch > 0x7E) return false;
  return t.size() <= (size_t)CAM_NODE_TEXT_MAX;
}

CamPacketCheck camCheckNodeInfo(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, CamNodeInfo& out) {
  S2oInfo i;
  if (len <= CAM_SIGNATURE_LEN || !s2oReadInfo(d, len - CAM_SIGNATURE_LEN, i)) return CAM_PACKET_INVALID;
  CamNodeInfo info;
  info.everyS = i.everyS;
  info.saving = i.saving;
  info.restart = i.restart;
  info.restarts = i.restarts;
  info.responseMs = i.responseMs;
  info.chip = i.chip;
  info.refused = i.refused;
  info.sensor = i.sensor;
  info.version = i.version;
  out = info;
  return checkSignature(mac, d, len, key);
}

bool camParseNodeInfo(const std::string& text, CamNodeInfo& out) {
  CamNodeInfo info;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (line.empty()) continue;
    size_t eq = line.find('=');
    std::string key = line.substr(0, eq), value = eq == std::string::npos ? std::string() : line.substr(eq + 1);
    if (!printable(value)) return false;
    unsigned long n = strtoul(value.c_str(), nullptr, 10);
    if (key == "every")
      info.everyS = (uint32_t)n;
    else if (key == "saving")
      info.saving = value == "1";
    else if (key == "restart")
      info.restart = n < CAM_RESTART_KINDS ? (uint8_t)n : CAM_RESTART_UNKNOWN;
    else if (key == "restarts")
      info.restarts = (uint16_t)n;
    else if (key == "chip")
      info.chip = (uint16_t)n;
    else if (key == "refused")
      info.refused = (uint32_t)n;
    else if (key == "sensor")
      info.sensor = value;
    else if (key == "version")
      info.version = value;
    else
      return false;
  }
  out = info;
  return true;
}

std::string camNodeInfoText(const CamNodeInfo& i) {
  return "every=" + std::to_string(i.everyS) + "\nsaving=" + (i.saving ? "1" : "0") + "\nrestart=" + std::to_string(i.restart) +
         "\nrestarts=" + std::to_string(i.restarts) + "\nchip=" + std::to_string(i.chip) + "\nrefused=" + std::to_string(i.refused) +
         "\nsensor=" + i.sensor + "\nversion=" + i.version + "\n";
}

const char* camRestartName(uint8_t reason) {
  static const char* const NAMES[CAM_RESTART_KINDS] = {"unknown", "power on", "reset button", "software", "crash", "watchdog", "brownout"};
  return reason < CAM_RESTART_KINDS ? NAMES[reason] : NAMES[0];
}

std::string camChipName(uint16_t chip) {
  switch (chip) {
    case 0: return "ESP32";
    case 2: return "ESP32-S2";
    case 5: return "ESP32-C3";
    case 9: return "ESP32-S3";
    case 12: return "ESP32-C2";
    case 13: return "ESP32-C6";
    case 16: return "ESP32-H2";
    case CAM_CHIP_UNKNOWN: return "unknown";
    default: return "chip " + std::to_string(chip);
  }
}

uint32_t camAutoSilentS(uint32_t everyS) {
  if (!everyS) return 0;
  uint64_t s = (uint64_t)everyS + (everyS / 4 > 600 ? everyS / 4 : 600);
  return s < CAM_MIN_SILENT_S ? CAM_MIN_SILENT_S : s > CAM_MAX_SILENT_S ? CAM_MAX_SILENT_S : (uint32_t)s;
}

void camConfirmationInto(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key,
                         uint8_t out[CAM_CONFIRMATION_LEN]) {
  camConfirmationBody(r, extra, out);
  s2oAddSignature(keyOf(key), sensorMac, out, CAM_CONFIRMATION_BODY);
}

std::string camConfirmation(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key) {
  uint8_t out[CAM_CONFIRMATION_LEN];
  camConfirmationInto(sensorMac, r, extra, key, out);
  return std::string((const char*)out, sizeof(out));
}

int camSeal(const uint8_t mac[6], uint32_t number, const uint8_t* inner, int innerLen, const std::string& key, bool fromHub, uint8_t* out) {
  return s2oSeal(keyOf(key), mac, number, inner, innerLen, fromHub, out);
}

int camOpenSealed(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, bool fromHub, uint8_t* out, int outMax) {
  int inner = s2oOpen(keyOf(key), mac, d, len, fromHub, out, outMax - CAM_SIGNATURE_LEN);
  return inner > 0 ? s2oAddSignature(keyOf(key), mac, out, inner) : inner;
}

CamPacketCheck camCheckFirmwareAsk(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, CamFirmwareAsk& out) {
  return s2oReadAsk(d, len, out.image, out.offset) ? checkSignature(mac, d, len, key) : CAM_PACKET_INVALID;
}

int camFirmwarePart(const uint8_t nodeMac[6], uint32_t image, uint32_t size, uint32_t offset, const uint8_t* part, int partLen,
                    const std::string& key, uint8_t out[CAM_FIRMWARE_PART_LEN_MAX]) {
  return s2oPart(keyOf(key), nodeMac, image, size, offset, part, partLen, out);
}

bool camCheckNodeImage(const std::function<bool(size_t, char*, size_t)>& read, size_t size, CamNodeImage& out) {
  static const size_t BLOCK = 1024;
  const size_t markLen = strlen(CAM_NODE_IMAGE_MARK), keep = markLen + CAM_NODE_TEXT_MAX;
  if (size < 64 || size > CAM_MAX_NODE_IMAGE) return false;
  // Read in blocks, each with the end of the one before, so a version that spans two is still found.
  char buf[BLOCK + 64];
  size_t carried = 0;
  uint32_t hash = 2166136261u;
  std::string version;
  bool found = false;
  uint16_t chip = 0;
  for (size_t at = 0; at < size;) {
    size_t n = size - at < BLOCK ? size - at : BLOCK;
    if (!read(at, buf + carried, n)) return false;
    if (at == 0) {
      if ((uint8_t)buf[0] != 0xE9) return false;  // an ESP32 app image starts so
      chip = readU16((const uint8_t*)buf + 12);
    }
    for (size_t i = 0; i < n; i++) hash = (hash ^ (uint8_t)buf[carried + i]) * 16777619u;
    size_t total = carried + n;
    for (size_t i = 0; !found && i + markLen <= total; i++) {
      if (memcmp(buf + i, CAM_NODE_IMAGE_MARK, markLen) != 0) continue;
      size_t j = i + markLen;
      while (j < total && buf[j] && j - (i + markLen) <= (size_t)CAM_NODE_TEXT_MAX) j++;
      if (j == total && at + n < size) break;  // the version runs into the next block, which brings it whole
      version.assign(buf + i + markLen, j - (i + markLen));
      found = true;
    }
    at += n;
    carried = total < keep ? total : keep;
    memmove(buf, buf + total - carried, carried);
  }
  if (!found || version.empty() || !printable(version)) return false;
  out.chip = chip;
  out.version = version;
  out.number = hash ? hash : 1;
  out.size = (uint32_t)size;
  return true;
}

static bool smallNumber(const std::string& s, int& out) {  // digits only, up to 9999
  if (s.empty() || s.size() > 4) return false;
  for (char ch : s)
    if (ch < '0' || ch > '9') return false;
  out = atoi(s.c_str());
  return true;
}

static bool minuteOfDay(const std::string& s, int& out) { return smallNumber(s, out) && out < 1440; }

bool camParseSensorSettings(const std::string& text, CamSensorSettings& out) {
  CamSensorSettings s;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    size_t eq = line.find('=');
    std::string key = line.substr(0, eq), value = eq == std::string::npos ? std::string() : line.substr(eq + 1);
    size_t dash = value.find('-');
    int hold = 0;
    if (key == "hold" && smallNumber(value, hold) && hold >= 1 && (uint32_t)hold <= CAM_MAX_HOLD_S) {
      s.holdS = (uint32_t)hold;
    } else if (key == "quiet" && dash != std::string::npos && minuteOfDay(value.substr(0, dash), s.quietFrom) &&
               minuteOfDay(value.substr(dash + 1), s.quietTo) && s.quietFrom != s.quietTo) {
    } else if (key == "zone" && (value == "interior" || value == "perimeter")) {
      s.interior = value == "interior";
    } else {
      return false;
    }
  }
  out = s;
  return true;
}

std::string camSensorSettingsText(const CamSensorSettings& s) {
  std::string out;
  if (s.holdS) out += "hold=" + std::to_string(s.holdS) + "\n";
  if (s.quietFrom >= 0) out += "quiet=" + std::to_string(s.quietFrom) + "-" + std::to_string(s.quietTo) + "\n";
  if (s.interior) out += "zone=interior\n";
  return out;
}

bool camParseNodeSettings(const std::string& text, CamNodeSettings& out) {
  CamNodeSettings s;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    size_t eq = line.find('=');
    std::string key = line.substr(0, eq), value = eq == std::string::npos ? std::string() : line.substr(eq + 1);
    if (value.empty() || value.size() > 6) return false;
    for (char ch : value)
      if (ch < '0' || ch > '9') return false;
    uint32_t n = (uint32_t)atol(value.c_str());
    if (key == "late" && n >= 1 && n <= CAM_MAX_LATE_S)
      s.lateS = n;
    else if (key == "silent" && n >= CAM_MIN_SILENT_S && n <= CAM_MAX_SILENT_S)
      s.silentS = n;
    else if (key == "every" && n >= CAM_MIN_EVERY_S && n <= CAM_MAX_EVERY_S)
      s.everyS = n;
    else if (key == "repeat" && n >= 1 && n <= CAM_MAX_REPEAT_S)
      s.repeatS = n;
    else
      return false;
  }
  out = s;
  return true;
}

std::string camNodeSettingsText(const CamNodeSettings& s) {
  auto line = [](const char* key, uint32_t v) { return v ? std::string(key) + "=" + std::to_string(v) + "\n" : std::string(); };
  return line("late", s.lateS) + line("silent", s.silentS) + line("every", s.everyS) + line("repeat", s.repeatS);
}

bool camQuiet(const CamSensorSettings& s, int minute) {
  if (minute < 0 || s.quietFrom < 0) return false;
  if (s.quietFrom < s.quietTo) return minute >= s.quietFrom && minute < s.quietTo;
  return minute >= s.quietFrom || minute < s.quietTo;  // across midnight
}

bool camParseBatteryLog(const std::string& text, std::vector<CamBatteryPoint>& out) {
  std::vector<CamBatteryPoint> points;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (line.empty()) continue;
    char* rest = nullptr;
    CamBatteryPoint p;
    p.t = strtoll(line.c_str(), &rest, 10);
    long percent = rest && *rest == ' ' ? strtol(rest + 1, &rest, 10) : -1;
    if (!rest || *rest || percent < 0 || percent > 100 || p.t <= 0) return false;
    p.percent = (uint8_t)percent;
    points.push_back(p);
  }
  out = points;
  return true;
}

std::string camBatteryLogText(const std::vector<CamBatteryPoint>& points) {
  std::string out;
  for (const CamBatteryPoint& p : points) out += std::to_string((long long)p.t) + " " + std::to_string(p.percent) + "\n";
  return out;
}

bool camAddBatteryPoint(std::vector<CamBatteryPoint>& points, int64_t t, uint8_t percent) {
  if (percent > 100) return false;
  if (!points.empty() && percent >= points.back().percent + 10) points.clear();  // a new or recharged battery
  if (!points.empty() && t - points.back().t < CAM_BATTERY_POINT_EVERY_S && t >= points.back().t) return false;
  CamBatteryPoint p;
  p.t = t;
  p.percent = percent;
  points.push_back(p);
  if ((int)points.size() > CAM_BATTERY_POINTS) points.erase(points.begin(), points.end() - CAM_BATTERY_POINTS);
  return true;
}

int camBatteryDaysLeft(const std::vector<CamBatteryPoint>& points) {
  if (points.empty()) return -1;
  const int64_t last = points.back().t, from = last - 30 * 86400;
  double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
  int64_t first = last;
  for (const CamBatteryPoint& p : points) {
    if (p.t < from) continue;
    double x = (double)(p.t - last) / 86400.0, y = p.percent;  // days, from the last reading
    n++;
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    if (p.t < first) first = p.t;
  }
  if (n < 4 || last - first < 3 * 86400) return -1;
  double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);                      // percent a day
  if (!(slope < 0) || -slope * (double)(last - first) / 86400.0 < 3) return -1;  // not clearly going down: noise, or a solar panel keeping up
  double now = (sy - slope * sx) / n;                                            // the line's value at the last reading
  double days = now / -slope;
  return days < 0 ? 0 : days > 9999 ? 9999 : (int)days;
}

uint8_t reportDirection(const SensorReport& r) {
  if (r.targetCount == 0) return 0;
  int best = 0;
  for (int i = 1; i < r.targetCount; i++)
    if (r.targets[i].distanceCm < r.targets[best].distanceCm) best = i;
  return r.targets[best].direction;
}

bool SensorBoard::trigger(int id, uint32_t now, uint8_t direction, bool delayed, uint32_t holdMs, bool several) {
  if (id < 1 || id > count()) return false;
  Entry& s = sensors_[id - 1];
  s.on = true;
  s.until = now + (holdMs ? holdMs : holdMs_);
  s.direction = direction;
  s.delayed = delayed;
  s.several = several;
  return true;
}

bool SensorBoard::pastEvent(int id) const { return id >= 1 && id <= count() && sensors_[id - 1].on && sensors_[id - 1].delayed; }

bool SensorBoard::several(int id) const { return id >= 1 && id <= count() && sensors_[id - 1].on && sensors_[id - 1].several; }

bool SensorBoard::clear(int id) {
  if (id < 1 || id > count()) return false;
  sensors_[id - 1].on = false;
  return true;
}

void SensorBoard::tick(uint32_t now) {
  for (Entry& s : sensors_)
    if (s.on && (int32_t)(s.until - now) <= 0) s.on = false;
}

bool SensorBoard::active(int id) const { return id >= 1 && id <= count() && sensors_[id - 1].on; }

bool SensorBoard::anyActive() const {
  for (const Entry& s : sensors_)
    if (s.on) return true;
  return false;
}

bool SensorBoard::shown(uint32_t now, int& id, uint8_t& direction, bool& delayed) const {
  int on = 0;
  for (const Entry& s : sensors_) on += s.on;
  if (!on) return false;
  int skip = (int)((now / CYCLE_MS) % (uint32_t)on);  // which of the active sensors has its turn
  for (int i = 0; i < count(); i++) {
    if (!sensors_[i].on || skip-- > 0) continue;
    id = i + 1;
    direction = sensors_[i].direction;
    delayed = sensors_[i].delayed;
    return true;
  }
  return false;
}

void CamDelayedQueue::add(const uint8_t mac[6], uint8_t input, uint8_t direction, bool timeKnown, uint32_t happenedAt, uint32_t holdMs, bool several,
                          bool replay) {
  CamDelayed* same = nullptr;
  for (int n = 0; n < count_; n++) {
    CamDelayed& d = slot(n);
    if (d.input != input || memcmp(d.mac, mac, 6) != 0) continue;
    int32_t apart = (int32_t)(happenedAt - d.happenedAt);
    if (timeKnown && d.timeKnown && (uint32_t)(apart < 0 ? -apart : apart) <= holdMs) {
      same = &d;
      break;
    }
    if (count_ == SIZE) same = &d;
  }
  if (!same && count_ < SIZE) {
    same = &slot(count_++);
    *same = CamDelayed();
    memcpy(same->mac, mac, 6);
    same->input = input;
    same->timeKnown = timeKnown;
    same->happenedAt = happenedAt;
    same->replay = true;  // until an alert not yet tried joins it
  }
  if (!same) {  // full of other sensors' alerts
    slot(count_ - 1).several = true;
    slot(count_ - 1).replay = slot(count_ - 1).replay && replay;
    return;
  }
  same->direction = direction;
  same->several = same->several || several;
  same->replay = same->replay && replay;
  if (timeKnown && same->timeKnown && (int32_t)(happenedAt - same->happenedAt) > 0) same->happenedAt = happenedAt;
}

bool CamDelayedQueue::take(CamDelayed& out) {
  if (!count_) return false;
  out = items_[head_];
  head_ = (head_ + 1) % SIZE;
  count_--;
  return true;
}

bool MotionSignal::update(bool active, uint32_t now) {
  bool send = !started_ || active != last_ || (active && now - sentAt_ >= repeatMs_);
  started_ = true;
  last_ = active;
  if (send) sentAt_ = now;
  return send;
}
