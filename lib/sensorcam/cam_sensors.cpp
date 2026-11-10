#include "cam_sensors.h"
#include <string.h>

static const uint32_t CYCLE_MS = 2000;
static const int HEADER_LEN = 13;
static const int TARGET_LEN = 3;

static uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool parseSensorPacket(const uint8_t* d, int len, SensorReport& out) {
  if (len < HEADER_LEN || d[0] != 1 || d[1] == 0 || d[2] > 1 || d[12] > CAM_MAX_TARGETS) return false;
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

uint8_t reportDirection(const SensorReport& r) {
  if (r.targetCount == 0) return 0;
  int best = 0;
  for (int i = 1; i < r.targetCount; i++)
    if (r.targets[i].distanceCm < r.targets[best].distanceCm) best = i;
  return r.targets[best].direction;
}

bool SensorBoard::trigger(int id, uint32_t now, uint8_t direction, bool delayed) {
  if (id < 1 || id > count()) return false;
  Entry& s = sensors_[id - 1];
  s.on = true;
  s.until = now + holdMs_;
  s.direction = direction;
  s.delayed = delayed;
  return true;
}

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
  std::vector<int> on;
  for (int i = 0; i < count(); i++)
    if (sensors_[i].on) on.push_back(i);
  if (on.empty()) return false;
  int i = on[(now / CYCLE_MS) % on.size()];
  id = i + 1;
  direction = sensors_[i].direction;
  delayed = sensors_[i].delayed;
  return true;
}

bool MotionSignal::update(bool active, uint32_t now) {
  bool send = !started_ || active != last_ || (active && now - sentAt_ >= repeatMs_);
  started_ = true;
  last_ = active;
  if (send) sentAt_ = now;
  return send;
}
