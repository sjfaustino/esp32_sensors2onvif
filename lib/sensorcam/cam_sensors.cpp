#include "cam_sensors.h"

static const uint32_t CYCLE_MS = 2000;

bool parseSensorPacket(const uint8_t* data, int len, int& id, bool& active) {
  if (len != 5 || data[0] != 'S' || data[1] != 'C' || data[2] != 1 || data[4] > 1) return false;
  id = data[3];
  active = data[4] == 1;
  return true;
}

bool SensorBoard::trigger(int id, uint32_t now) {
  if (id < 1 || id > count()) return false;
  sensors_[id - 1].on = true;
  sensors_[id - 1].until = now + holdMs_;
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

int SensorBoard::frameIndex(uint32_t now) const {
  std::vector<int> on;
  for (int i = 0; i < count(); i++)
    if (sensors_[i].on) on.push_back(i + 1);
  if (on.empty()) return 0;
  return on[(now / CYCLE_MS) % on.size()];
}

bool MotionSignal::update(bool active, uint32_t now) {
  bool send = !started_ || active != last_ || (active && now - sentAt_ >= repeatMs_);
  started_ = true;
  last_ = active;
  if (send) sentAt_ = now;
  return send;
}
