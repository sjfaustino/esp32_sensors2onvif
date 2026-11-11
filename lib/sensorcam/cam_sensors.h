#pragma once
#include <stdint.h>
#include <string>
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

// Reads version 1 and 2 messages. It does not check a version 2 signature; camCheckPacket does.
bool parseSensorPacket(const uint8_t* data, int len, SensorReport& out);

// Version 2 is version 1 followed by an 8-byte signature: the first 8 bytes of HMAC-SHA1, keyed with the hub's
// ESP-NOW key, over the sender's MAC address (6 bytes) and then the message before the signature.
const int CAM_SIGNATURE_LEN = 8;
enum CamPacketCheck { CAM_PACKET_INVALID, CAM_PACKET_UNSIGNED, CAM_PACKET_SIGNED, CAM_PACKET_BAD_SIGNATURE };
CamPacketCheck camCheckPacket(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, SensorReport& out);
// The signature for a message of len bytes, without its own signature. For senders and tests.
std::string camPacketSignature(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key);

// Per-sensor settings, kept as settings.txt in the sensor's folder: "hold=SECONDS" and "quiet=FROM-TO" lines,
// FROM and TO being minutes after local midnight. During quiet hours an alert is logged but not sent to the NVR.
struct CamSensorSettings {
  uint32_t holdS = 0;  // how long an alert stays active; 0 uses the hub's default
  int quietFrom = -1;  // -1 for no quiet hours
  int quietTo = -1;
};
const uint32_t CAM_MAX_HOLD_S = 3600;
bool camParseSensorSettings(const std::string& text, CamSensorSettings& out);  // false for anything the page would refuse
std::string camSensorSettingsText(const CamSensorSettings& s);
bool camQuiet(const CamSensorSettings& s, int localMinute);  // localMinute -1, an unknown clock, is never quiet

// Direction of the nearest target, which is the one shown in the picture.
uint8_t reportDirection(const SensorReport& r);

// Sensor state. Ids are 1-based slots in the hub's sensor table.
class SensorBoard {
 public:
  SensorBoard(int count, uint32_t holdMs) : holdMs_(holdMs), sensors_(count) {}

  // Starts or extends the hold, for holdMs or else the default. Returns false for an unknown id.
  bool trigger(int id, uint32_t now, uint8_t direction = 0, bool delayed = false, uint32_t holdMs = 0);
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
