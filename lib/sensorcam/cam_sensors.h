#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_frames.h"
#include "cam_timing.h"
#include "s2o_messages.h"

const int CAM_MAX_TARGETS = S2O_MAX_TARGETS;
const int CAM_MAX_SENSORS = 32;  // the hub's sensor table
const uint32_t CAM_AGE_UNKNOWN = S2O_AGE_UNKNOWN;
const uint16_t CAM_DISTANCE_UNKNOWN = S2O_DISTANCE_UNKNOWN;
const uint8_t CAM_BATTERY_UNKNOWN = S2O_BATTERY_UNKNOWN;
inline uint8_t camBatteryPercent(int value) { return value >= 0 && value <= 100 ? (uint8_t)value : CAM_BATTERY_UNKNOWN; }

// One report from a device, as the library shared with the nodes reads it (s2o_messages.h).
using SensorTarget = S2oTarget;
using SensorReport = S2oReport;

// A report on the air is signed as s2o_messages.h says. parseSensorPacket reads it without checking the signature;
// camCheckPacket checks it too.
const uint8_t CAM_REPORT = S2O_REPORT;
const int CAM_SIGNATURE_LEN = S2O_SIG_LEN;
bool parseSensorPacket(const uint8_t* data, int len, SensorReport& out);
enum CamPacketCheck { CAM_PACKET_INVALID, CAM_PACKET_SIGNED, CAM_PACKET_BAD_SIGNATURE };
CamPacketCheck camCheckPacket(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, SensorReport& out);
// The signature for a message of len bytes, without its own signature. For senders and tests.
std::string camPacketSignature(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key);

// What the hub sends back once it has accepted a report, so the sensor can stop resending, and what it has for the node.
// The layout is in s2o_messages.h.
const uint8_t CAM_CONFIRMATION = S2O_CONFIRMATION;
const int CAM_CONFIRMATION_BODY = S2O_CONFIRMATION_BODY;
const int CAM_CONFIRMATION_LEN = S2O_CONFIRMATION_LEN;
using CamForNode = S2oForNode;
inline void camConfirmationBody(const SensorReport& r, const CamForNode& extra, uint8_t out[CAM_CONFIRMATION_BODY]) {
  s2oConfirmationBody(r, extra, out);
}
std::string camConfirmation(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key);
// Without using the heap. out gets the body and its signature.
void camConfirmationInto(const uint8_t sensorMac[6], const SensorReport& r, const CamForNode& extra, const std::string& key,
                         uint8_t out[CAM_CONFIRMATION_LEN]);

// A sealed message hides what a report says from anyone listening; see s2o_messages.h.
const uint8_t CAM_SEALED = S2O_SEALED;
const int CAM_SEALED_EXTRA = S2O_SEALED_EXTRA;  // the type and the number
const int CAM_SEALED_MAX_INNER = S2O_SEALED_MAX_INNER;
// The message inside, with a signature made with key added, so it reads as if sent plain: its length, 0 for one that is
// not a sealed message, or -1 for a wrong signature. mac is always the node's. Without using the heap.
int camOpenSealed(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, bool fromHub, uint8_t* out, int outMax);
// The sealed form of inner, which has no signature: its length, CAM_SEALED_EXTRA + innerLen + CAM_SIGNATURE_LEN.
int camSeal(const uint8_t mac[6], uint32_t number, const uint8_t* inner, int innerLen, const std::string& key, bool fromHub, uint8_t* out);

// A node fetches a firmware update from the hub in parts; see s2o_messages.h.
const uint8_t CAM_FIRMWARE_ASK = S2O_FIRMWARE_ASK, CAM_FIRMWARE_PART = S2O_FIRMWARE_PART;
const uint32_t CAM_FIRMWARE_DESCRIBE = S2O_DESCRIBE;
const int CAM_FIRMWARE_ASK_LEN = S2O_ASK_LEN;
const int CAM_FIRMWARE_PART_HEAD = S2O_PART_HEAD;
const int CAM_FIRMWARE_PART_MAX = S2O_PART_MAX;
const int CAM_FIRMWARE_PART_LEN_MAX = S2O_PART_LEN_MAX;
const int CAM_FIRMWARE_WINDOW = S2O_WINDOW;
struct CamFirmwareAsk {
  uint32_t image = 0, offset = 0;
};
CamPacketCheck camCheckFirmwareAsk(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, CamFirmwareAsk& out);
// Its length.
int camFirmwarePart(const uint8_t nodeMac[6], uint32_t image, uint32_t size, uint32_t offset, const uint8_t* part, int partLen,
                    const std::string& key, uint8_t out[CAM_FIRMWARE_PART_LEN_MAX]);

// A node firmware file put on the hub: an ESP32 app image, with its chip in the header and its version after the text
// CAM_NODE_IMAGE_MARK, as the node's firmware has it. Its number is the FNV-1a hash of the whole file, never 0.
const char* const CAM_NODE_IMAGE_MARK = S2O_NODE_IMAGE_MARK;
const uint32_t CAM_PILOT_S = 86400;  // a day
struct CamNodeImage {
  uint16_t chip = 0;
  std::string version;
  uint32_t number = 0, size = 0;
  // Tried on one node first: offered to the others only once that node has run it for CAM_PILOT_S, on the hub's clock,
  // so a bad update cannot take every node out at once. pilot is its folder, empty for every node at once.
  std::string pilot;
  uint32_t pilotRan = 0;  // when the hub first heard the pilot run it; 0 not yet
  bool passed(uint32_t now) const { return pilot.empty() || (pilotRan && now >= pilotRan + CAM_PILOT_S); }
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
  std::string camera;     // the NVR camera to look at for this sensor's alerts, as "camera=" text; empty for none
};
const uint32_t CAM_MAX_HOLD_S = 3600;
const size_t CAM_MAX_CAMERA = 32;                                              // bytes of the camera's name
bool camValidCamera(const std::string& s);                                     // 1 to 32 bytes, with no control characters
bool camParseSensorSettings(const std::string& text, CamSensorSettings& out);  // false for anything the page would refuse
std::string camSensorSettingsText(const CamSensorSettings& s);
bool camQuiet(const CamSensorSettings& s, int localMinute);  // localMinute -1, an unknown clock, is never quiet

// Per-node settings, kept as settings.txt in the node's folder, as "late=SECONDS", "silent=SECONDS", "every=SECONDS"
// and "repeat=SECONDS" lines. An alert the node reports as having happened longer ago than late is a past event: it gets
// the past-event picture and its own NVR record. A node not heard from for silent is reported silent, which may mean it
// was disabled. every and repeat go to the node with each confirmation: how often it reports with nothing to report, and
// how often while a sensor stays on. "hw=NUMBER,SENSOR,SENSORPIN,TAMPERPIN,BATTERYPIN,CELL" is its hardware, as set on
// the Sensors page (S2oHardware; 255 for no pin), which the node fetches when it runs other settings.
struct CamNodeSettings {
  uint32_t lateS = 0;    // 0 uses the hub's default
  uint32_t silentS = 0;  // 0 uses the hub's default, 3 hours
  uint32_t everyS = 0;   // 0 leaves it to the node
  uint32_t repeatS = 0;  // the same
  bool hwSet = false;    // hardware settings made on the hub
  S2oHardware hw;
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
const uint8_t CAM_NODE_INFO = S2O_INFO;
const int CAM_NODE_TEXT_MAX = S2O_TEXT_MAX;
const int CAM_NODE_INFO_HEAD = S2O_INFO_HEAD;
const int CAM_NODE_INFO_MAX = S2O_INFO_MAX;
enum CamNodeRestart {
  CAM_RESTART_UNKNOWN,
  CAM_RESTART_POWER,
  CAM_RESTART_BUTTON,
  CAM_RESTART_SOFTWARE,
  CAM_RESTART_CRASH,
  CAM_RESTART_WATCHDOG,
  CAM_RESTART_BROWNOUT,
  CAM_RESTART_KINDS
};
const char* camRestartName(uint8_t reason);  // "brownout", for the page and the log
inline bool camRestartIsFault(uint8_t reason) {
  return reason == CAM_RESTART_CRASH || reason == CAM_RESTART_WATCHDOG || reason == CAM_RESTART_BROWNOUT;
}
const uint16_t CAM_RESPONSE_UNKNOWN = S2O_RESPONSE_UNKNOWN, CAM_CHIP_UNKNOWN = S2O_CHIP_UNKNOWN;
struct CamNodeInfo {
  uint32_t everyS = 0;
  bool saving = false;
  uint8_t restart = CAM_RESTART_UNKNOWN;
  uint16_t restarts = 0;
  uint16_t responseMs = CAM_RESPONSE_UNKNOWN;  // not kept in about.txt, as it changes with every alert
  uint16_t chip = CAM_CHIP_UNKNOWN;
  uint32_t refused = 0;
  std::string sensor, version;
  bool hasHardware = false;  // what the node says it runs; a node from before hardware settings says nothing
  S2oHardware hardware;
  // Everything about.txt keeps.
  bool operator==(const CamNodeInfo& o) const {
    return everyS == o.everyS && saving == o.saving && restart == o.restart && restarts == o.restarts && chip == o.chip && refused == o.refused &&
           sensor == o.sensor && version == o.version && hasHardware == o.hasHardware && (!hasHardware || hardware == o.hardware);
  }
};
CamPacketCheck camCheckNodeInfo(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key, CamNodeInfo& out);
bool camParseNodeInfo(const std::string& text, CamNodeInfo& out);
std::string camNodeInfoText(const CamNodeInfo& info);
// Hardware settings as "NUMBER,SENSOR,SENSORPIN,TAMPERPIN,BATTERYPIN,CELL", 255 for no pin.
std::string camHardwareText(const S2oHardware& hw);
// A node asking for its hardware settings (S2O_HARDWARE_ASK), and the hub's answer, signed with the node's key.
CamPacketCheck camCheckHardwareAsk(const uint8_t mac[6], const uint8_t* data, int len, const std::string& key);
void camHardwareMessage(const uint8_t mac[6], const S2oHardware& hw, const std::string& key, uint8_t out[S2O_HARDWARE_LEN]);
bool camParseHardware(const std::string& text, S2oHardware& out);
// The silent time worked out from how often a node reports: a quarter longer, and at least 10 minutes longer, so one late
// report is not enough. 0 when the node does not say. Its own setting on the Sensors page comes first.
uint32_t camAutoSilentS(uint32_t everyS);

// A node is overdue once its regular report is a fifth of its time late, and a minute more: sooner than silent, which
// waits long enough to be sure. Only for the jamming check below.
uint32_t camOverdueMs(uint32_t everyS);

// Batteries run out one node at a time. Several nodes that stop reporting together, after reporting steadily, point to
// something stopping them all, such as a jammer on 2.4 GHz. Each pass gives every node's state by its position; two or
// more becoming overdue within CAM_JAM_WINDOW_MS of each other are reported once, until no node is overdue that
// recently. How soon it can tell is set by how often the nodes report.
class CamJamWatch {
 public:
  // Returns how many nodes went quiet together when that is to be reported now, else 0. together(n) then says which.
  int update(const bool overdue[], int count, uint32_t now);
  bool together(int n) const { return on_[n] && now_ - at_[n] < CAM_JAM_WINDOW_MS; }

 private:
  bool on_[CAM_MAX_SENSORS] = {false};
  uint32_t at_[CAM_MAX_SENSORS] = {0};
  uint32_t now_ = 0;
  bool reported_ = false;
};

// A sensor that fires many times an hour is more likely faulty or badly aimed than watching that many intruders, and its
// alerts bury the real ones. Counted over an hour from its first alert; reported once, and again only after an hour with
// fewer than a quarter as many. Every alert still goes through: this only says something may be wrong.
struct CamBusyCount {
  bool started = false, reported = false;
  uint32_t from = 0;
  int count = 0;
  bool note(uint32_t now, int limit);  // one new alert; true when it should be reported now
};

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
