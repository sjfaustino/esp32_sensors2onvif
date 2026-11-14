#include "cam_sensors.h"
#include <stdlib.h>
#include <string.h>
#include "cam_auth.h"

static const uint32_t CYCLE_MS = 2000;
static const int HEADER_LEN = 13;
static const int TARGET_LEN = 3;

static uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool parseSensorPacket(const uint8_t* d, int len, SensorReport& out) {
  if (len < 1 || (d[0] != 1 && d[0] != 2)) return false;
  if (d[0] == 2) len -= CAM_SIGNATURE_LEN;
  if (len < HEADER_LEN || d[1] == 0 || d[2] > 1 || d[12] > CAM_MAX_TARGETS) return false;
  if (len != HEADER_LEN + d[12] * TARGET_LEN) return false;
  SensorReport r;
  r.input = d[1];
  r.active = d[2] == 1;
  r.battery = camBatteryPercent(d[3]);
  r.counter = readU32(d + 4);
  r.age = readU32(d + 8);
  r.targetCount = d[12];
  for (int i = 0; i < r.targetCount; i++) {
    const uint8_t* t = d + HEADER_LEN + i * TARGET_LEN;
    if (t[2] >= CAM_DIRECTIONS) return false;
    r.targets[i].distanceCm = (uint16_t)(t[0] | (t[1] << 8));
    r.targets[i].direction = t[2];
  }
  out = r;
  return true;
}

// Without using the heap, so an alert is checked and confirmed even when memory has run out. out gets the full 20 bytes.
static void signature(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, uint8_t out[20]) {
  camHmacSha1((const uint8_t*)key.data(), key.size(), mac, 6, d, (size_t)len, out);
}

std::string camPacketSignature(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key) {
  uint8_t s[20];
  signature(mac, d, len, key, s);
  return std::string((const char*)s, CAM_SIGNATURE_LEN);
}

CamPacketCheck camCheckPacket(const uint8_t mac[6], const uint8_t* d, int len, const std::string& key, SensorReport& out) {
  if (!parseSensorPacket(d, len, out)) return CAM_PACKET_INVALID;
  if (d[0] == 1) return CAM_PACKET_UNSIGNED;
  uint8_t expected[20];
  signature(mac, d, len - CAM_SIGNATURE_LEN, key, expected);
  uint8_t differ = 0;  // every byte is compared, so the time taken does not reveal how much matched
  for (int i = 0; i < CAM_SIGNATURE_LEN; i++) differ |= expected[i] ^ d[len - CAM_SIGNATURE_LEN + i];
  return differ || key.empty() ? CAM_PACKET_BAD_SIGNATURE : CAM_PACKET_SIGNED;
}

void camConfirmationInto(const uint8_t sensorMac[6], const SensorReport& r, const std::string& key, uint8_t out[CAM_CONFIRMATION_LEN]) {
  uint8_t d[7] = {CAM_CONFIRMATION, r.input, (uint8_t)r.active, (uint8_t)r.counter, (uint8_t)(r.counter >> 8), (uint8_t)(r.counter >> 16), (uint8_t)(r.counter >> 24)};
  uint8_t s[20];
  signature(sensorMac, d, sizeof(d), key, s);
  memcpy(out, d, sizeof(d));
  memcpy(out + sizeof(d), s, CAM_SIGNATURE_LEN);
}

std::string camConfirmation(const uint8_t sensorMac[6], const SensorReport& r, const std::string& key) {
  uint8_t out[CAM_CONFIRMATION_LEN];
  camConfirmationInto(sensorMac, r, key, out);
  return std::string((const char*)out, sizeof(out));
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
    if (key == "late" && n >= 1 && n <= CAM_MAX_LATE_S) s.lateS = n;
    else if (key == "silent" && n >= CAM_MIN_SILENT_S && n <= CAM_MAX_SILENT_S) s.silentS = n;
    else return false;
  }
  out = s;
  return true;
}

std::string camNodeSettingsText(const CamNodeSettings& s) {
  return (s.lateS ? "late=" + std::to_string(s.lateS) + "\n" : std::string()) + (s.silentS ? "silent=" + std::to_string(s.silentS) + "\n" : std::string());
}

bool camQuiet(const CamSensorSettings& s, int minute) {
  if (minute < 0 || s.quietFrom < 0) return false;
  if (s.quietFrom < s.quietTo) return minute >= s.quietFrom && minute < s.quietTo;
  return minute >= s.quietFrom || minute < s.quietTo;  // across midnight
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

void CamDelayedQueue::add(const uint8_t mac[6], uint8_t input, uint8_t direction, bool timeKnown, uint32_t happenedAt, uint32_t holdMs, bool several, bool replay) {
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
