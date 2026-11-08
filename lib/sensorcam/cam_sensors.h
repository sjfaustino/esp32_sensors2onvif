#pragma once
#include <stdint.h>
#include <vector>

// ESP-NOW sensor message, 5 bytes: 'S', 'C', version (1), sensor id, state (1 = alert, 0 = clear).
bool parseSensorPacket(const uint8_t* data, int len, int& id, bool& active);

// Sensor state. Ids are 1-based and match sensors.json.
class SensorBoard {
 public:
  SensorBoard(int count, uint32_t holdMs) : holdMs_(holdMs), sensors_(count) {}

  // Starts or extends the hold. Returns false for an unknown id.
  bool trigger(int id, uint32_t now);
  bool clear(int id);
  void tick(uint32_t now);  // ends holds that have run out
  bool active(int id) const;
  bool anyActive() const;
  int count() const { return (int)sensors_.size(); }

  // Frame to show: 0 when clear, otherwise an active sensor's id, cycling when several are active.
  int frameIndex(uint32_t now) const;

 private:
  struct Entry {
    bool on = false;
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
