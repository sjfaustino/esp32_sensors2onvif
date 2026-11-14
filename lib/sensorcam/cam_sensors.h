#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "cam_frames.h"

const int CAM_MAX_TARGETS = 3;
const int CAM_MAX_SENSORS = 32;  // the hub's sensor table
const uint32_t CAM_AGE_UNKNOWN = 0xFFFFFFFFu;
const uint16_t CAM_DISTANCE_UNKNOWN = 0xFFFF;
const uint8_t CAM_BATTERY_UNKNOWN = 0xFF;
inline uint8_t camBatteryPercent(int value) { return value >= 0 && value <= 100 ? (uint8_t)value : CAM_BATTERY_UNKNOWN; }

struct SensorTarget {
  uint16_t distanceCm = CAM_DISTANCE_UNKNOWN;
  uint8_t direction = 0;
};

// One report from a device. On the air, little-endian, 13 + 3 * targetCount bytes and the signature:
// CAM_REPORT, input (1-255), active (0/1), battery %, counter (u32), age seconds (u32),
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

// A report on the air: CAM_REPORT, then the fields above, then an 8-byte signature: the first 8 bytes of HMAC-SHA1, keyed
// with the hub's ESP-NOW key or the node's own, over the sender's MAC address (6 bytes) and then the bytes before the
// signature. parseSensorPacket reads it without checking the signature; camCheckPacket checks it too.
const uint8_t CAM_REPORT = 2;
const int CAM_SIGNATURE_LEN = 8;
bool parseSensorPacket(const uint8_t* data, int len, SensorReport& out);
enum CamPacketCheck { CAM_PACKET_INVALID, CAM_PACKET_SIGNED, CAM_PACKET_BAD_SIGNATURE };
CamPacketCheck camCheckPacket(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, SensorReport& out);
// The signature for a message of len bytes, without its own signature. For senders and tests.
std::string camPacketSignature(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key);

// What the hub sends back once it has accepted a report, so the sensor can stop resending. 15 bytes: 3, the input, active
// (0/1), the report number (u32), then the signature over the sensor's own MAC address and those 7 bytes.
const uint8_t CAM_CONFIRMATION = 3;
const int CAM_CONFIRMATION_LEN = 7 + CAM_SIGNATURE_LEN;
std::string camConfirmation(const uint8_t sensorMac[6], const SensorReport& r, const std::string& key);
void camConfirmationInto(const uint8_t sensorMac[6], const SensorReport& r, const std::string& key, uint8_t out[CAM_CONFIRMATION_LEN]);  // without using the heap

// Per-sensor settings, kept as settings.txt in the sensor's folder: "hold=SECONDS", "quiet=FROM-TO" and "zone=interior"
// lines, FROM and TO being minutes after local midnight. During quiet hours an alert is logged but not sent to the NVR,
// and so is an interior sensor's while the hub is armed home.
struct CamSensorSettings {
  uint32_t holdS = 0;  // how long an alert stays active; 0 uses the hub's default
  int quietFrom = -1;  // -1 for no quiet hours
  int quietTo = -1;
  bool interior = false;  // otherwise perimeter, which is watched in every armed mode
};
const uint32_t CAM_MAX_HOLD_S = 3600;
bool camParseSensorSettings(const std::string& text, CamSensorSettings& out);  // false for anything the page would refuse
std::string camSensorSettingsText(const CamSensorSettings& s);
bool camQuiet(const CamSensorSettings& s, int localMinute);  // localMinute -1, an unknown clock, is never quiet

// Per-node settings, kept as settings.txt in the node's folder, as "late=SECONDS" and "silent=SECONDS" lines.
// An alert the node reports as having happened longer ago than late is a past event: it gets the past-event picture and
// its own NVR record. A node not heard from for silent is reported silent, which may mean it was disabled.
struct CamNodeSettings {
  uint32_t lateS = 0;    // 0 uses the hub's default
  uint32_t silentS = 0;  // 0 uses the hub's default, 3 hours
};
const uint32_t CAM_MAX_LATE_S = 86400;
const uint32_t CAM_MIN_SILENT_S = 60, CAM_MAX_SILENT_S = 7 * 86400;
bool camParseNodeSettings(const std::string& text, CamNodeSettings& out);  // false for anything the page would refuse
std::string camNodeSettingsText(const CamNodeSettings& s);

// Direction of the nearest target, which is the one shown in the picture.
uint8_t reportDirection(const SensorReport& r);

// Sensor state. Ids are 1-based slots in the hub's sensor table.
class SensorBoard {
 public:
  SensorBoard(int count, uint32_t holdMs) : holdMs_(holdMs), sensors_(count) {}

  // Starts or extends the hold, for holdMs or else the default. Returns false for an unknown id.
  // several: a past-event record standing for more than one sensor, shown with the built-in past-event picture.
  bool trigger(int id, uint32_t now, uint8_t direction = 0, bool delayed = false, uint32_t holdMs = 0, bool several = false);
  bool clear(int id);
  void tick(uint32_t now);  // ends holds that have run out
  bool active(int id) const;
  bool anyActive() const;
  int count() const { return (int)sensors_.size(); }

  // The active sensor to show, cycling when several are active. False when none is.
  bool shown(uint32_t now, int& id, uint8_t& direction, bool& delayed) const;
  bool several(int id) const;
  bool pastEvent(int id) const;  // active, as a past event

 private:
  struct Entry {
    bool on = false;
    bool delayed = false;
    bool several = false;
    uint8_t direction = 0;
    uint32_t until = 0;
  };
  uint32_t holdMs_;
  std::vector<Entry> sensors_;
};

// Past events waiting for their own NVR records, oldest first. Kept by device and input, because a deleted sensor's
// table position can pass to a new one.
struct CamDelayed {
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  uint8_t input = 0;
  uint8_t direction = 0;
  bool several = false;     // other sensors' alerts have joined it, because the queue was full
  bool replay = false;      // only alerts the NVR already missed once, which were logged and sent then
  bool timeKnown = false;   // the report gave its age, so when it happened is known
  uint32_t happenedAt = 0;  // on the millis() clock; the latest of the alerts merged into this one
};

class CamDelayedQueue {
 public:
  static const int SIZE = 16;
  // A late alert from one sensor within holdMs of a waiting one from that sensor joins it, as it would have live.
  // When the queue is full it joins a waiting one from that sensor, or else the newest waiting record, which then
  // stands for several sensors. So every alert gets a record, and the count never passes SIZE.
  void add(const uint8_t mac[6], uint8_t input, uint8_t direction, bool timeKnown, uint32_t happenedAt, uint32_t holdMs, bool several = false,
           bool replay = false);
  bool take(CamDelayed& out);  // the oldest; false when none is waiting
  int count() const { return count_; }
  const CamDelayed& at(int n) const { return items_[(head_ + n) % SIZE]; }  // n from 0, the oldest, to count() - 1

 private:
  CamDelayed& slot(int n) { return items_[(head_ + n) % SIZE]; }
  CamDelayed items_[SIZE];
  int head_ = 0, count_ = 0;
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
