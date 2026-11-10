#pragma once
#include <stdint.h>
#include <vector>
#include "cam_frames.h"

const int CAM_MAX_TARGETS = 3;
const uint32_t CAM_AGE_UNKNOWN = 0xFFFFFFFFu;
const uint16_t CAM_DISTANCE_UNKNOWN = 0xFFFF;
const uint8_t CAM_BATTERY_UNKNOWN = 0xFF;
inline uint8_t camBatteryPercent(int value) { return value >= 0 && value <= 100 ? (uint8_t)value : CAM_BATTERY_UNKNOWN; }

struct SensorTarget {
  uint16_t distanceCm = CAM_DISTANCE_UNKNOWN;
  uint8_t direction = 0;
};

// One report from a device. On the air, little-endian, 13 + 3 * targetCount bytes:
// version (1), input (1-255), active (0/1), battery %, counter (u32), age seconds (u32),
// target count (0-3), then for each target distance cm (u16) and direction (0-4).
struct SensorReport {
  uint8_t input = 1;
  bool active = false;
  uint8_t battery = CAM_BATTERY_UNKNOWN;
  uint32_t counter = 0;
  uint32_t age = 0;
  uint8_t targetCount = 0;
  SensorTarget targets[CAM_MAX_TARGETS];
};

bool parseSensorPacket(const uint8_t* data, int len, SensorReport& out);

// Direction of the nearest target, which is the one shown in the picture.
uint8_t reportDirection(const SensorReport& r);

// Sensor state. Ids are 1-based slots in the hub's sensor table.
class SensorBoard {
 public:
  SensorBoard(int count, uint32_t holdMs) : holdMs_(holdMs), sensors_(count) {}

  // Starts or extends the hold. Returns false for an unknown id.
  bool trigger(int id, uint32_t now, uint8_t direction = 0, bool delayed = false);
  bool clear(int id);
  void tick(uint32_t now);  // ends holds that have run out
  bool active(int id) const;
  bool anyActive() const;
  int count() const { return (int)sensors_.size(); }

  // The active sensor to show, cycling when several are active. False when none is.
  bool shown(uint32_t now, int& id, uint8_t& direction, bool& delayed) const;

 private:
  struct Entry {
    bool on = false;
    bool delayed = false;
    uint8_t direction = 0;
    uint32_t until = 0;
  };
  uint32_t holdMs_;
  std::vector<Entry> sensors_;
};

// Decides when a motion notification goes out: on every change, and repeatedly while active.
// The NVR ends its event about 2 s after a "true" regardless of later messages, so a long
// trigger must keep sending "true" more often than that.
class MotionSignal {
 public:
  explicit MotionSignal(uint32_t repeatMs) : repeatMs_(repeatMs) {}
  bool update(bool active, uint32_t now);

 private:
  uint32_t repeatMs_;
  bool started_ = false;
  bool last_ = false;
  uint32_t sentAt_ = 0;
};
