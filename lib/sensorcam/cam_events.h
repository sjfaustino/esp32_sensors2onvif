// The event history: one log on storage, added to and never rewritten, which the Events page, the CSV download and the
// restart all read.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <string>
#include "cam_sensors.h"
#include "cam_storage.h"

const time_t CAM_CLOCK_VALID_AFTER = 1577836800;  // 2020-01-01: earlier means the clock has not been set

// What an event is. The numbers are stored in the log and sent as "kind", so they never change.
enum CamEventKind : uint8_t {
  CAM_EVENT_REPORT = 0,  // an alert or a clear report
  CAM_EVENT_SILENT = 1,
  CAM_EVENT_LOW_BATTERY = 2,
  CAM_EVENT_QUIET_ALERT = 3,  // an alert in quiet hours: logged, not sent to the NVR
  CAM_EVENT_WEAK_SIGNAL = 4,
  CAM_EVENT_NOT_DELIVERED = 5,  // an alert the NVR was not listening for
  CAM_EVENT_NVR_GONE = 6,       // the hub's own events from here on
  CAM_EVENT_NVR_BACK = 7,
  CAM_EVENT_HUB_STARTED = 8,
  CAM_EVENT_NETWORK_LOST = 9,  // the hub was off its Wi-Fi network for a while, without restarting
};
// The one name for each event, used by the history, the CSV file, the web page and the webhook.
const char* camEventName(uint8_t kind, bool active, bool delayed);

// The one wording of each event's notification. who is the sensor, as "Gate (AA-BB-CC-DD-EE-01/1)", or for the hub's own
// events the hub's name. detail and offS are those of a hub event.
std::string camEventMessage(uint8_t kind, const std::string& who, const std::string& detail, uint32_t offS, bool delayed);

// A length of time as people say it: "7 min", "2 h 13 min", "3 days 4 h". Rounded up to the minute.
std::string camDurationText(uint32_t seconds);

struct CamEvent {
  time_t received = 0;
  uint32_t age = 0;      // how long ago it happened, as the sensor said
  char sensor[22] = "";  // AA-BB-CC-DD-EE-FF/255, or "hub" for the hub's own events
  // A sensor event's sensor name, or a hub event's detail, such as why the hub started. Sent as "name" or "detail".
  char text[26] = "";
  uint8_t kind = CAM_EVENT_REPORT;  // a CamEventKind
  bool active = false;
  bool delayed = false;
  uint8_t battery = CAM_BATTERY_UNKNOWN;
  uint8_t targetCount = 0;
  SensorTarget targets[CAM_MAX_TARGETS];
  uint32_t offS = CAM_AGE_UNKNOWN;  // how long the hub was off or off the network, at most, for those events
};

// One event on storage: 96 bytes, little-endian, the same on every machine, with a checksum so a record cut short by a
// power cut is found and skipped.
const size_t CAM_EVENT_RECORD = 96;
void camEventEncode(const CamEvent& e, uint8_t out[CAM_EVENT_RECORD]);
bool camEventDecode(const uint8_t in[CAM_EVENT_RECORD], CamEvent& out);  // false for a damaged record
std::string camEventCsvLine(const CamEvent& e);                         // with its line break

class CamEventLog {
 public:
  static const int RECENT = 64;                // the newest, kept in memory for the Events page
  static const size_t FILE_MAX = 256 * 1024;  // then the log becomes the older log, so about 5400 events are kept

  explicit CamEventLog(const CamStorage& storage) : storage_(storage) {}
  // Reads the newest events. Converts the history of firmware from before this log, once.
  void load();
  void add(const CamEvent& e);  // kept in memory until save()
  // Adds what is waiting to the log. Starting a new log is put off while a download reads the old one. False when
  // storage refused, in which case the events stay waiting and the next save tries again.
  bool save(bool downloading);
  bool waiting() const { return !pending_.empty(); }
  bool saveFailed() const { return saveFailed_; }
  int count() const { return count_; }
  const CamEvent& recent(int n) const { return ring_[(next_ - 1 - n + 2 * RECENT) % RECENT]; }  // 0 is the newest
  size_t bytesOnStorage() const { return oldBytes_ + curBytes_; }

  // The whole history as CSV: after csvBegin, csvRead hands it out piece by piece, in order or repeating a piece, and
  // gives fewer bytes than asked only at the end. Events added meanwhile are left for the next download.
  void csvBegin();
  size_t csvRead(size_t offset, char* out, size_t len);

 private:
  bool recordAt(size_t index, CamEvent& e);  // index over the older log, then the current one
  void remember(const CamEvent& e);
  const CamStorage& storage_;
  CamEvent ring_[RECENT];
  int count_ = 0, next_ = 0;
  std::string pending_;  // encoded records not yet on storage
  bool saveFailed_ = false;
  size_t oldBytes_ = 0, curBytes_ = 0;        // whole records in each log; a cut-off record after them is written over
  // The download in progress: a snapshot of the sizes, and where it has got to.
  size_t legacyOld_ = 0, legacyCur_ = 0, oldRecords_ = 0, allRecords_ = 0;
  size_t lineStart_ = 0, lineIndex_ = 0;  // the line being handed out, and the record after it
  std::string line_;
  bool hasMark_ = false;  // where the last piece began, for a piece asked for again
  size_t markStart_ = 0, markIndex_ = 0;
  std::string markLine_;
  std::string chunk_;  // records read ahead from storage
  size_t chunkFirst_ = 0;
};
