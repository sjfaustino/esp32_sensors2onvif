#pragma once
#include <stdint.h>
#include <functional>
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

// What the hub sends back once it has accepted a report, so the sensor can stop resending, and what it has for the node.
// 22 bytes and the signature over the sensor's own MAC address and those bytes: 3, the input, active (0/1), the report
// number (u32), then the hub's time (u32, seconds since 1970 UTC, 0 while its clock is not set), the node's report time
// set on the hub (u32 seconds, 0 for the node's own), its repeat time while a sensor stays on (u16 seconds, 0 for its own),
// the end of a walk test started on the hub (u32, on the hub's time, 0 for none), and flags: bit 0, a firmware update is
// ready for the node.
const uint8_t CAM_CONFIRMATION = 3;
const int CAM_CONFIRMATION_BODY = 22;
const int CAM_CONFIRMATION_LEN = CAM_CONFIRMATION_BODY + CAM_SIGNATURE_LEN;
struct CamForNode {
  uint32_t hubTime = 0;
  uint32_t everyS = 0;
  uint16_t repeatS = 0;
  uint32_t walkUntil = 0;
  bool update = false;
};
void camConfirmationBody(const SensorReport& r, const CamForNode& extra, uint8_t out[CAM_CONFIRMATION_BODY]);
std::string camConfirmation(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key);
// Without using the heap. out gets the body and its signature.
void camConfirmationInto(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key, uint8_t out[CAM_CONFIRMATION_LEN]);

// A sealed message hides what a report says from anyone listening: CAM_SEALED, a number used once by its sender (u32),
// then the message inside, without its own signature, XORed with a key stream, then the signature over the node's MAC
// address and every byte before it. Block i of the stream, 20 bytes, is HMAC-SHA1 keyed with the seal key over the
// direction (0 to the hub, 1 from it), the number (u32) and i (one byte). The seal key is HMAC-SHA1, keyed with the
// signing key, over "sensors2onvif seal". Inside: a report or node info to the hub, a confirmation from it.
const uint8_t CAM_SEALED = 7;
const int CAM_SEALED_EXTRA = 5;  // the type and the number
const int CAM_SEALED_MAX_INNER = 120;
// The message inside, with a signature made with key added, so it reads as if sent plain: its length, 0 for one that is
// not a sealed message, or -1 for a wrong signature. mac is always the node's. Without using the heap.
int camOpenSealed(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, bool fromHub, uint8_t* out, int outMax);
// The sealed form of inner, which has no signature: its length, CAM_SEALED_EXTRA + innerLen + CAM_SIGNATURE_LEN.
int camSeal(const uint8_t mac[6], uint32_t number, const uint8_t* inner, int innerLen, const std::string& key, bool fromHub, uint8_t* out);

// A node fetches a firmware update from the hub in parts. It asks with CAM_FIRMWARE_ASK, the image's number (u32, 0 when
// it does not know it yet) and an offset (u32), signed like a report; CAM_FIRMWARE_DESCRIBE as the offset asks what the
// image is. The hub answers with up to CAM_FIRMWARE_WINDOW parts from that offset on: CAM_FIRMWARE_PART, the image's
// number (u32), its size (u32), the offset (u32), then up to CAM_FIRMWARE_PART_MAX bytes of it, signed like a
// confirmation. A description is answered with one part at CAM_FIRMWARE_DESCRIBE, holding the image's version.
const uint8_t CAM_FIRMWARE_ASK = 8, CAM_FIRMWARE_PART = 9;
const uint32_t CAM_FIRMWARE_DESCRIBE = 0xFFFFFFFFu;
const int CAM_FIRMWARE_ASK_LEN = 9 + CAM_SIGNATURE_LEN;
const int CAM_FIRMWARE_PART_HEAD = 13;
const int CAM_FIRMWARE_PART_MAX = 200;
const int CAM_FIRMWARE_PART_LEN_MAX = CAM_FIRMWARE_PART_HEAD + CAM_FIRMWARE_PART_MAX + CAM_SIGNATURE_LEN;
const int CAM_FIRMWARE_WINDOW = 4;
struct CamFirmwareAsk {
  uint32_t image = 0, offset = 0;
};
CamPacketCheck camCheckFirmwareAsk(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, CamFirmwareAsk& out);
// Its length.
int camFirmwarePart(const uint8_t nodeMac[6], uint32_t image, uint32_t size, uint32_t offset, const uint8_t* part, int partLen, const std::string& key,
                    uint8_t out[CAM_FIRMWARE_PART_LEN_MAX]);

// A node firmware file put on the hub: an ESP32 app image, with its chip in the header and its version after the text
// CAM_NODE_IMAGE_MARK, as the node's firmware has it. Its number is the FNV-1a hash of the whole file, never 0.
const char* const CAM_NODE_IMAGE_MARK = "sensors2onvif-node-version=";
struct CamNodeImage {
  uint16_t chip = 0;
  std::string version;
  uint32_t number = 0, size = 0;
};
const uint32_t CAM_MAX_NODE_IMAGE = 3 * 1024 * 1024;
// read(offset, out, len) reads exactly len bytes of the file. False for a file that is not a node's firmware.
bool camCheckNodeImage(const std::function<bool(size_t, char*, size_t)>& read, size_t size, CamNodeImage& out);
std::string camChipName(uint16_t chip);  // "ESP32-C6", or a number for one this hub does not name

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

// Per-node settings, kept as settings.txt in the node's folder, as "late=SECONDS", "silent=SECONDS", "every=SECONDS"
// and "repeat=SECONDS" lines. An alert the node reports as having happened longer ago than late is a past event: it gets
// the past-event picture and its own NVR record. A node not heard from for silent is reported silent, which may mean it
// was disabled. every and repeat go to the node with each confirmation: how often it reports with nothing to report, and
// how often while a sensor stays on.
struct CamNodeSettings {
  uint32_t lateS = 0;    // 0 uses the hub's default
  uint32_t silentS = 0;  // 0 uses the hub's default, 3 hours
  uint32_t everyS = 0;   // 0 leaves it to the node
  uint32_t repeatS = 0;  // the same
};
const uint32_t CAM_MAX_LATE_S = 86400;
const uint32_t CAM_MIN_EVERY_S = 60, CAM_MAX_EVERY_S = 86400;
const uint32_t CAM_MAX_REPEAT_S = 60;
const uint32_t CAM_MIN_SILENT_S = 60, CAM_MAX_SILENT_S = 7 * 86400;
bool camParseNodeSettings(const std::string& text, CamNodeSettings& out);  // false for anything the page would refuse
std::string camNodeSettingsText(const CamNodeSettings& s);

// What a node says about itself. It sends this with its regular reports, so the hub learns it again after any change; the
// hub keeps it as about.txt in the node's folder. On the air: CAM_NODE_INFO, how often it reports in seconds (u32, 0 when
// it does not say), flags (bit 0: saving its battery, so reporting less often), why it last restarted (CamNodeRestart),
// how many times it has restarted (u16), how long its last alert took to be confirmed in milliseconds (u16, 0xFFFF for
// none), its chip as in its firmware's header (u16, 0xFFFF unknown), the firmware update it refused because it was
// undone (u32, 0 for none), the length (0 to 24) and text of its sensor's kind, the same for its firmware version, then a
// report's 8-byte signature. Texts are printable ASCII.
const uint8_t CAM_NODE_INFO = 6;
const int CAM_NODE_TEXT_MAX = 24;
const int CAM_NODE_INFO_HEAD = 17;
const int CAM_NODE_INFO_MAX = CAM_NODE_INFO_HEAD + 2 * (1 + CAM_NODE_TEXT_MAX) + CAM_SIGNATURE_LEN;
enum CamNodeRestart { CAM_RESTART_UNKNOWN, CAM_RESTART_POWER, CAM_RESTART_BUTTON, CAM_RESTART_SOFTWARE, CAM_RESTART_CRASH, CAM_RESTART_WATCHDOG, CAM_RESTART_BROWNOUT, CAM_RESTART_KINDS };
const char* camRestartName(uint8_t reason);  // "brownout", for the page and the log
inline bool camRestartIsFault(uint8_t reason) { return reason == CAM_RESTART_CRASH || reason == CAM_RESTART_WATCHDOG || reason == CAM_RESTART_BROWNOUT; }
const uint16_t CAM_RESPONSE_UNKNOWN = 0xFFFF, CAM_CHIP_UNKNOWN = 0xFFFF;
struct CamNodeInfo {
  uint32_t everyS = 0;
  bool saving = false;
  uint8_t restart = CAM_RESTART_UNKNOWN;
  uint16_t restarts = 0;
  uint16_t responseMs = CAM_RESPONSE_UNKNOWN;  // not kept in about.txt, as it changes with every alert
  uint16_t chip = CAM_CHIP_UNKNOWN;
  uint32_t refused = 0;
  std::string sensor, version;
  // Everything about.txt keeps.
  bool operator==(const CamNodeInfo& o) const {
    return everyS == o.everyS && saving == o.saving && restart == o.restart && restarts == o.restarts && chip == o.chip && refused == o.refused &&
           sensor == o.sensor && version == o.version;
  }
};
CamPacketCheck camCheckNodeInfo(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, CamNodeInfo& out);
bool camParseNodeInfo(const std::string& text, CamNodeInfo& out);
std::string camNodeInfoText(const CamNodeInfo& info);
// The silent time worked out from how often a node reports: a quarter longer, and at least 10 minutes longer, so one late
// report is not enough. 0 when the node does not say. Its own setting on the Sensors page comes first.
uint32_t camAutoSilentS(uint32_t everyS);

// A node's battery readings, a few a day, kept as battery.txt in its folder as "SECONDS PERCENT" lines, the time since
// 1970 UTC, so the hub can tell how long the battery will last.
struct CamBatteryPoint {
  int64_t t = 0;
  uint8_t percent = 0;
};
const int CAM_BATTERY_POINTS = 90;
const int64_t CAM_BATTERY_POINT_EVERY_S = 6 * 3600;
bool camParseBatteryLog(const std::string& text, std::vector<CamBatteryPoint>& out);
std::string camBatteryLogText(const std::vector<CamBatteryPoint>& points);
// Adds a reading when the last is at least CAM_BATTERY_POINT_EVERY_S old. A rise of 10 points or more is a new or
// recharged battery, so the readings before it go. True when the log changed.
bool camAddBatteryPoint(std::vector<CamBatteryPoint>& points, int64_t t, uint8_t percent);
// Days until the battery is empty, from a straight line through the readings of the last 30 days: -1 while that cannot
// be told, as with fewer than 4 readings, or over less than 3 days, or a battery that has not fallen by 3 points.
int camBatteryDaysLeft(const std::vector<CamBatteryPoint>& points);

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
