#include "cam_events.h"
#include <stdio.h>
#include <string.h>
#include "cam_soap.h"
#include "cam_text.h"

static const char LOG[] = "events.log";
static const char LOG_OLD[] = "events-old.log";
static const char LEGACY_CSV[] = "events.csv";  // the history of firmware before this log; read, never written
static const char LEGACY_CSV_OLD[] = "events-old.csv";
static const char LEGACY_BIN[] = "events.bin";  // that firmware's newest events, converted once
static const char CSV_HEADER[] = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
static const uint8_t RECORD_VERSION = 1;
static const size_t PENDING_MAX = 128 * CAM_EVENT_RECORD;  // while storage refuses, the oldest waiting ones go first
// Room kept for events waiting to be saved, so logging an alert needs no memory: they are saved on the next pass.
static const size_t PENDING_KEPT = 8 * CAM_EVENT_RECORD;

const char* camEventName(uint8_t kind, bool active, bool delayed) {
  switch (kind) {
    case CAM_EVENT_SILENT: return "went silent";
    case CAM_EVENT_LOW_BATTERY: return "low battery";
    case CAM_EVENT_QUIET_ALERT: return "alert in quiet hours";
    case CAM_EVENT_WEAK_SIGNAL: return "weak signal";
    case CAM_EVENT_NOT_DELIVERED: return delayed ? "delayed alert not delivered" : "alert not delivered";
    case CAM_EVENT_NVR_GONE: return "NVR not listening";
    case CAM_EVENT_NVR_BACK: return "NVR listening again";
    case CAM_EVENT_HUB_STARTED: return "hub started";
    case CAM_EVENT_NETWORK_LOST: return "network lost";
    case CAM_EVENT_DISARMED_ALERT: return "alert while disarmed";
    case CAM_EVENT_ARMED: return "armed";
    case CAM_EVENT_DISARMED: return "disarmed";
    case CAM_EVENT_HOME_ALERT: return "alert while armed home";
    case CAM_EVENT_ARMED_HOME: return "armed home";
    case CAM_EVENT_TEST_ALERT: return "test alert";
    case CAM_EVENT_RESTART_LOOP: return "restarting often";
    case CAM_EVENT_NODE_FAULT: return "node restarted by a fault";
    case CAM_EVENT_BATTERY_RUNNING_OUT: return "battery running out";
    case CAM_EVENT_JAMMING: return "possible jamming";
    case CAM_EVENT_BUSY_SENSOR: return "firing often";
    default: return !active ? "clear" : delayed ? "delayed alert" : "alert";
  }
}

std::vector<const char*> camEventNames() {
  std::vector<const char*> names;
  for (int kind = 0; kind < CAM_EVENT_KIND_COUNT; kind++)
    for (int variant = 0; variant < 4; variant++) {
      const char* name = camEventName((uint8_t)kind, (variant & 1) == 0, (variant & 2) != 0);
      bool seen = false;
      for (const char* n : names) seen = seen || strcmp(n, name) == 0;
      if (!seen) names.push_back(name);
    }
  return names;
}

// The leading number of a detail such as "3 nodes", or -1.
static long long leadingNumber(const std::string& s) {
  long long n = 0;
  size_t i = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') n = n * 10 + (s[i++] - '0');
  return i ? n : -1;
}

// Where an arm or a disarm came from, and for how long, as the hub keeps it: "the web page", or "the web page, for 1 h",
// as camMinutesText says the time. In the message's language.
static std::string fromText(const std::string& detail) {
  size_t comma = detail.find(", for ");
  if (comma == std::string::npos) return camText(detail.c_str());
  std::string from = detail.substr(0, comma), time = detail.substr(comma + 6);
  long long n = leadingNumber(time);
  std::string unit = n < 0 ? std::string() : time.substr(std::to_string(n).size());
  std::string said = unit == " min"                      ? camText("{0} min", {std::to_string(n)})
                     : unit == " h"                      ? camText("{0} h", {std::to_string(n)})
                     : unit == " day" || unit == " days" ? camCount(n, "{0} day", "{0} days")
                                                         : time;
  return camText("{0}, for {1}", {camText(from.c_str()), said});
}

std::string camEventMessage(uint8_t kind, const std::string& who, const std::string& detail, uint32_t offS, bool delayed) {
  bool known = offS != CAM_AGE_UNKNOWN;
  switch (kind) {
    case CAM_EVENT_NOT_DELIVERED: return camText("{0}: {1}, because the NVR is not listening", {who, camText(camEventName(kind, true, delayed))});
    case CAM_EVENT_NVR_GONE: return camText("The NVR is not listening to {0}: alerts are not reaching it", {who});
    case CAM_EVENT_NVR_BACK: return camText("The NVR is listening to {0} again", {who});
    case CAM_EVENT_HUB_STARTED: {
      std::string why = detail.empty() ? camText("unknown") : camText(detail.c_str());
      return known ? camText("{0} started ({1}), after being off for up to {2}", {who, why, camDurationText(offS)})
                   : camText("{0} started ({1})", {who, why});
    }
    case CAM_EVENT_NETWORK_LOST:
      return camText("{0} was off its Wi-Fi network for up to {1}; alerts in that time reach the NVR as past events once it is back",
                     {who, known ? camDurationText(offS) : std::string(camText("an unknown time"))});
    case CAM_EVENT_ARMED: return detail.empty() ? camText("{0} was armed", {who}) : camText("{0} was armed from {1}", {who, fromText(detail)});
    case CAM_EVENT_RESTART_LOOP:
      return camText("{0} has restarted by itself {1} times in an hour: something keeps stopping it, so alerts may be missed",
                     {who, std::to_string(leadingNumber(detail))});
    case CAM_EVENT_NODE_FAULT:
      return camText("{0}: the node restarted by a fault, such as a brownout from a weak battery or a bad wire; see the Nodes and pairing page",
                     {who});
    case CAM_EVENT_BATTERY_RUNNING_OUT: return camText("{0}: the battery will run out within a week", {who});
    case CAM_EVENT_JAMMING:
      return camText(
          "{0}: {1} nodes stopped reporting at the same time. Batteries fail one at a time, so something may be blocking the radio, such as a "
          "jammer",
          {who, std::to_string(leadingNumber(detail))});
    case CAM_EVENT_BUSY_SENSOR:
      return camText("{0}: fired {1} times within an hour. Every alert still goes to the NVR, but check the sensor's aim and wiring",
                     {who, std::to_string(CAM_BUSY_ALERTS)});
    case CAM_EVENT_ARMED_HOME:
      return detail.empty() ? camText("{0} was armed home: interior sensors are logged but not sent to the NVR", {who})
                            : camText("{0} was armed home from {1}: interior sensors are logged but not sent to the NVR", {who, fromText(detail)});
    case CAM_EVENT_DISARMED:
      return detail.empty() ? camText("{0} was disarmed: alerts are logged but not sent to the NVR", {who})
                            : camText("{0} was disarmed from {1}: alerts are logged but not sent to the NVR", {who, fromText(detail)});
    default: return camText("{0}: {1}", {who, camText(camEventName(kind, true, delayed))});
  }
}

static std::string joined(const std::vector<std::string>& items) {
  std::string out;
  for (size_t i = 0; i < items.size(); i++) out += (i ? ", " : "") + items[i];
  return out;
}

static const char* modeText(const std::string& mode) {
  return camText(mode == "disarmed" ? "disarmed" : mode == "home" ? "armed home" : "armed away");
}

// The last test alert, and a word to send one when it is over a month old, or none is known.
static std::string testText(long long agoS, bool remind) {
  if (agoS < 0)
    return remind ? camText("no test alert in the last month: send one from the hub's Overview, to see that the NVR still records")
                  : camText("no test alert this week");
  if (remind && agoS > CAM_TEST_REMIND_AFTER_S)
    return camText("last test alert {0} ago, over a month: send one from the hub's Overview, to see that the NVR still records",
                   {camDurationText((uint32_t)agoS)});
  return camText("last test alert {0} ago", {camDurationText((uint32_t)agoS)});
}

std::string camDailyMessage(const CamDailyCheck& d) {
  std::string m = camText("{0} daily check: {1} of {2} nodes heard in the last day", {d.hub, std::to_string(d.heard), std::to_string(d.nodes)});
  m += "; " + (d.problems.empty() ? std::string(camText("no problems")) : camText("problems: {0}", {joined(d.problems)}));
  m += "; " + (d.allCounted ? camCount(d.alerts, "{0} alert in the last day", "{0} alerts in the last day")
                            : camCount(d.alerts, "at least {0} alert in the last day", "at least {0} alerts in the last day"));
  if (d.notDelivered)
    m += ", " + camText("{0} not delivered to the NVR", {std::to_string(d.notDelivered)});
  else if (d.alerts)
    m += std::string(", ") + camText("all delivered to the NVR");
  m += std::string("; ") + camText(d.nvrListening ? "the NVR is listening" : "the NVR is NOT listening");
  m += std::string("; ") + modeText(d.mode);
  m += "; " + testText(d.testAgoS, true);
  // Told only when low, as a figure every day would be read past; the JSON has it every day.
  if (d.memoryLow && d.memoryLow < CAM_MEMORY_LOW_WARN)
    m += "; " + camText("free memory fell to {0} KB, which risks dropped work: see Device info", {std::to_string(d.memoryLow / 1024)});
  return m;
}

std::string camWeeklyMessage(const CamWeeklySummary& w) {
  std::string m = camText("{0} weekly summary", {w.hub}) + ": " +
                  (w.allCounted ? camCount(w.alerts, "{0} alert in the last 7 days", "{0} alerts in the last 7 days")
                                : camCount(w.alerts, "at least {0} alert in the last 7 days", "at least {0} alerts in the last 7 days"));
  if (w.notDelivered)
    m += ", " + camText("{0} not delivered to the NVR", {std::to_string(w.notDelivered)});
  else if (w.alerts)
    m += std::string(", ") + camText("all delivered to the NVR");
  if (w.busiestAlerts) m += "; " + camText("most from {0} ({1})", {w.busiest, std::to_string(w.busiestAlerts)});
  if (w.held) m += "; " + camText("{0} more only logged, while disarmed, armed home or in quiet hours", {std::to_string(w.held)});
  m += "; " + camCount(w.problems, "{0} problem reported", "{0} problems reported");
  if (!w.batteries.empty()) m += "; " + camText("batteries running low: {0}", {joined(w.batteries)});
  m += "; " + (w.nowProblems.empty() ? std::string(camText("no problems now")) : camText("problems now: {0}", {joined(w.nowProblems)}));
  m += std::string("; ") + camText(w.nvrListening ? "the NVR is listening" : "the NVR is NOT listening");
  m += std::string("; ") + modeText(w.mode);
  m += "; " + testText(w.testAgoS, false);
  return m;
}

std::string camDurationText(uint32_t seconds) {
  uint32_t m = (seconds + 59) / 60;
  if (m < 60) return camText("{0} min", {std::to_string(m)});
  if (m < 48 * 60) return camText("{0} h {1} min", {std::to_string(m / 60), std::to_string(m % 60)});
  return camText("{0} days {1} h", {std::to_string(m / 1440), std::to_string(m / 60 % 24)});
}

// ---- one record ----

static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint32_t checksum(const uint8_t* p, size_t n) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

// 0 version, 1 kind, 2 flags (1 active, 2 delayed), 3 battery, 4 received (8), 12 age, 16 offS, 20 target count,
// 21 three targets of distance (2) and direction (1), 30 sensor (22), 52 text (26), 78 zeros, 92 checksum of bytes 0-91.
void camEventEncode(const CamEvent& e, uint8_t out[CAM_EVENT_RECORD]) {
  memset(out, 0, CAM_EVENT_RECORD);
  out[0] = RECORD_VERSION;
  out[1] = e.kind;
  out[2] = (uint8_t)((e.active ? 1 : 0) | (e.delayed ? 2 : 0));
  out[3] = e.battery;
  uint64_t received = (uint64_t)(int64_t)e.received;
  put32(out + 4, (uint32_t)received);
  put32(out + 8, (uint32_t)(received >> 32));
  put32(out + 12, e.age);
  put32(out + 16, e.offS);
  out[20] = e.targetCount;
  for (int i = 0; i < CAM_MAX_TARGETS; i++) {
    out[21 + 3 * i] = (uint8_t)e.targets[i].distanceCm;
    out[22 + 3 * i] = (uint8_t)(e.targets[i].distanceCm >> 8);
    out[23 + 3 * i] = e.targets[i].direction;
  }
  memcpy(out + 30, e.sensor, sizeof(e.sensor));
  memcpy(out + 52, e.text, sizeof(e.text));
  out[30 + sizeof(e.sensor) - 1] = 0;
  out[52 + sizeof(e.text) - 1] = 0;
  put32(out + 92, checksum(out, 92));
}

bool camEventDecode(const uint8_t in[CAM_EVENT_RECORD], CamEvent& out) {
  if (in[0] != RECORD_VERSION || get32(in + 92) != checksum(in, 92) || in[20] > CAM_MAX_TARGETS) return false;
  CamEvent e;
  e.kind = in[1];
  e.active = in[2] & 1;
  e.delayed = (in[2] & 2) != 0;
  e.battery = in[3];
  e.received = (time_t)(int64_t)((uint64_t)get32(in + 4) | ((uint64_t)get32(in + 8) << 32));
  e.age = get32(in + 12);
  e.offS = get32(in + 16);
  e.targetCount = in[20];
  for (int i = 0; i < CAM_MAX_TARGETS; i++) {
    e.targets[i].distanceCm = (uint16_t)(in[21 + 3 * i] | (in[22 + 3 * i] << 8));
    e.targets[i].direction = in[23 + 3 * i];
  }
  memcpy(e.sensor, in + 30, sizeof(e.sensor));
  memcpy(e.text, in + 52, sizeof(e.text));
  e.sensor[sizeof(e.sensor) - 1] = 0;
  e.text[sizeof(e.text) - 1] = 0;
  out = e;
  return true;
}

// Quoted when it holds a comma, quote or line break; a leading = + - or @ is kept from being read as a formula.
static std::string csvText(const std::string& s) {
  std::string out = !s.empty() && strchr("=+-@", s[0]) ? "'" + s : s;
  if (out.find_first_of(",\"\r\n") == std::string::npos) return out;
  std::string quoted = "\"";
  for (char ch : out) {
    if (ch == '"') quoted += '"';
    quoted += ch;
  }
  return quoted + "\"";
}

std::string camEventCsvLine(const CamEvent& e) {
  static const char* const DIRECTIONS[] = {"", "approaching", "moving away", "left to right", "right to left"};
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER, ageKnown = e.age != CAM_AGE_UNKNOWN;
  std::string what = camEventName(e.kind, e.active, e.delayed);
  if (e.offS != CAM_AGE_UNKNOWN && (e.kind == CAM_EVENT_HUB_STARTED || e.kind == CAM_EVENT_NETWORK_LOST))
    what += " for up to " + camDurationText(e.offS);
  int nearest = -1;
  for (int i = 0; i < e.targetCount; i++)
    if (nearest < 0 || e.targets[i].distanceCm < e.targets[nearest].distanceCm) nearest = i;
  std::string distance, direction;
  if (nearest >= 0) {
    const SensorTarget& t = e.targets[nearest];
    char metres[16];
    snprintf(metres, sizeof(metres), "%.2f", t.distanceCm / 100.0);
    if (t.distanceCm != CAM_DISTANCE_UNKNOWN) distance = metres;
    if (t.direction < CAM_DIRECTIONS) direction = DIRECTIONS[t.direction];
  }
  return (clockOk ? iso(e.received) : std::string()) + "," + (clockOk && ageKnown ? iso(e.received - (time_t)e.age) : std::string()) + "," +
         e.sensor + "," + csvText(e.text) + "," + what + "," + (e.battery == CAM_BATTERY_UNKNOWN ? std::string() : std::to_string(e.battery)) + "," +
         distance + "," + direction + "\n";
}

// ---- the log ----

void CamEventLog::remember(const CamEvent& e) {
  ring_[next_] = e;
  next_ = (next_ + 1) % RECENT;
  if (count_ < RECENT) count_++;
  added_++;
}

// The older firmware kept its newest 64 events as a copy of its memory. Its layout is CamEvent's, without offS on the
// oldest firmware, so each record is read into the start of one.
static void convertLegacy(const CamStorage& storage) {
  static const size_t OLD_SIZE = (offsetof(CamEvent, offS) + alignof(CamEvent) - 1) / alignof(CamEvent) * alignof(CamEvent);
  static_assert(OLD_SIZE != sizeof(CamEvent), "the saved size must tell the two layouts apart");
  std::string blob;
  uint32_t header[4];
  if (storage.readFile(LEGACY_BIN, blob, sizeof(header) + CamEventLog::RECENT * sizeof(CamEvent)) && blob.size() >= sizeof(header)) {
    memcpy(header, blob.data(), sizeof(header));
    size_t size = header[1];
    bool ok = header[0] == 0x31564543 && (size == sizeof(CamEvent) || size == OLD_SIZE) &&
              blob.size() == sizeof(header) + size * CamEventLog::RECENT && header[2] <= (uint32_t)CamEventLog::RECENT &&
              header[3] < (uint32_t)CamEventLog::RECENT;
    std::string records;
    for (uint32_t i = 0; ok && i < header[2]; i++) {  // oldest first
      size_t at = (header[3] + CamEventLog::RECENT - header[2] + i) % CamEventLog::RECENT;
      CamEvent e;
      memcpy((void*)&e, blob.data() + sizeof(header) + at * size, size == sizeof(CamEvent) ? size : offsetof(CamEvent, offS));
      if (size != sizeof(CamEvent)) e.offS = CAM_AGE_UNKNOWN;
      uint8_t r[CAM_EVENT_RECORD];
      camEventEncode(e, r);
      records.append((const char*)r, sizeof(r));
    }
    // Added after whatever the log holds: a file found beside the log was written by the older firmware after a failed
    // update was undone, so its events are newer. Removed only once converted. An unreadable file is left alone: it
    // is the user's data, and is tried again at the next start.
    size_t logSize = storage.fileSize(LOG);
    if (ok && storage.writeAt(LOG, logSize - logSize % CAM_EVENT_RECORD, records.data(), records.size())) storage.removeTree(LEGACY_BIN);
  }
}

void CamEventLog::load() {
  if (storage_.exists(LEGACY_BIN)) convertLegacy(storage_);
  size_t oldSize = storage_.fileSize(LOG_OLD), curSize = storage_.fileSize(LOG);
  oldBytes_ = oldSize - oldSize % CAM_EVENT_RECORD;  // a record cut short at the end is written over by the next one
  curBytes_ = curSize - curSize % CAM_EVENT_RECORD;
  // Only the newest are read, for the Events page. The rest stays on storage until someone downloads it.
  oldRecords_ = oldBytes_ / CAM_EVENT_RECORD;
  allRecords_ = oldRecords_ + curBytes_ / CAM_EVENT_RECORD;
  chunk_.clear();
  for (size_t i = allRecords_ > (size_t)RECENT ? allRecords_ - RECENT : 0; i < allRecords_; i++) {
    CamEvent e;
    if (recordAt(i, e)) remember(e);
  }
  std::string().swap(chunk_);
  pending_.reserve(PENDING_KEPT);
}

size_t camLogFileMax(size_t total, size_t keep) {
  const size_t least = 64 * CAM_EVENT_RECORD;
  size_t room = total > keep ? (total - keep) / 4 : 0;
  if (room >= CamEventLog::FILE_MAX) return CamEventLog::FILE_MAX;  // as always on a large board
  room -= room % CAM_EVENT_RECORD;
  return room > least ? room : least;
}

void CamEventLog::add(const CamEvent& e) {
  uint8_t r[CAM_EVENT_RECORD];
  camEventEncode(e, r);
  if (pending_.size() >= PENDING_MAX) pending_.erase(0, CAM_EVENT_RECORD);
  pending_.append((const char*)r, sizeof(r));  // first, so an event shown is one that will be saved
  remember(e);
}

bool CamEventLog::save(bool downloading) {
  if (pending_.empty()) return true;
  if (!downloading && curBytes_ + pending_.size() > fileMax_ && storage_.renameFile(LOG, LOG_OLD)) {
    oldBytes_ = curBytes_;
    curBytes_ = 0;
    rotations_++;
  }
  saveFailed_ = !storage_.writeAt(LOG, curBytes_, pending_.data(), pending_.size());
  // Refused, as by full storage: the older log goes for room, as it would at the next new log anyway, since a report
  // that cannot be saved cannot be confirmed. Not during a download, which reads both.
  if (saveFailed_ && !downloading && curBytes_ && storage_.renameFile(LOG, LOG_OLD)) {
    oldBytes_ = curBytes_;
    curBytes_ = 0;
    rotations_++;
    saveFailed_ = !storage_.writeAt(LOG, 0, pending_.data(), pending_.size());
  }
  if (saveFailed_) return false;
  curBytes_ += pending_.size();
  pending_.clear();
  if (pending_.capacity() > PENDING_KEPT) {  // grown while storage refused: given back, keeping the usual room
    std::string().swap(pending_);
    pending_.reserve(PENDING_KEPT);
  }
  return true;
}

bool CamEventLog::eachSince(time_t since, size_t most, const std::function<void(const CamEvent&)>& each) const {
  CamHistoryScan s;
  scanBegin(s, since, most);
  while (!scanStep(s, 64, each)) {
  }
  return s.complete;
}

void CamEventLog::scanBegin(CamHistoryScan& s, time_t since, size_t most) const {
  s = CamHistoryScan();
  s.since = since;
  s.most = most;
}

bool CamEventLog::scanStep(CamHistoryScan& s, size_t records, const std::function<void(const CamEvent&)>& each) const {
  CamEvent e;
  // 1 when an event older than since was found or most was reached, 0 to go on.
  auto take = [&](const uint8_t* record) {
    if (!camEventDecode(record, e) || e.received < CAM_CLOCK_VALID_AFTER) return 0;
    if (e.received < s.since) {
      s.complete = true;
      return 1;
    }
    each(e);
    return ++s.seen >= s.most ? 1 : 0;
  };
  auto over = [&]() {
    s.part = 3;
    return true;
  };
  if (s.part == 0) {  // those waiting, all at once, as they are in memory; and the current log as it is at this moment
    for (size_t end = pending_.size() - pending_.size() % CAM_EVENT_RECORD; end >= CAM_EVENT_RECORD; end -= CAM_EVENT_RECORD)
      if (take((const uint8_t*)pending_.data() + end - CAM_EVENT_RECORD)) return over();
    s.part = 1;
    s.left = curBytes_ / CAM_EVENT_RECORD;
    s.rotations = rotations_;
    return false;
  }
  if (s.part > 2) return true;
  if (rotations_ != s.rotations) {                                   // a new log was begun: the current one became the older one
    if (s.part == 2 || rotations_ - s.rotations > 1) return over();  // and the older one, being read, is gone
    s.part = 2;
    s.rotations = rotations_;
  }
  uint8_t chunk[8 * CAM_EVENT_RECORD];
  while (records && s.left) {
    size_t n = s.left < 8 ? s.left : 8;
    n = n < records ? n : records;
    s.left -= n;
    records -= n;
    if (!storage_.readAt(s.part == 2 ? LOG_OLD : LOG, s.left * CAM_EVENT_RECORD, (char*)chunk, n * CAM_EVENT_RECORD)) return over();
    for (size_t i = n; i-- > 0;)
      if (take(chunk + i * CAM_EVENT_RECORD)) return over();
  }
  if (s.left) return false;
  if (s.part == 2) return over();  // the history ends here
  s.part = 2;
  s.left = oldBytes_ / CAM_EVENT_RECORD;
  return false;
}

void CamEventLog::csvBegin(const std::string& sensor, time_t since) {
  bool chosen = !sensor.empty() || since;
  legacyOld_ = chosen ? 0 : storage_.fileSize(LEGACY_CSV_OLD);
  legacyCur_ = chosen ? 0 : storage_.fileSize(LEGACY_CSV);
  oldRecords_ = oldBytes_ / CAM_EVENT_RECORD;
  allRecords_ = oldRecords_ + curBytes_ / CAM_EVENT_RECORD;
  csvSensor_ = sensor;
  csvSince_ = since;
  chunk_.clear();
  // The records are in the order received. One saved before the clock was set counts as late, so halving never skips
  // past a later one that is wanted; it is left out below anyway.
  size_t lo = 0, hi = allRecords_;
  while (since && lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    CamEvent e;
    if (recordAt(mid, e) && e.received >= CAM_CLOCK_VALID_AFTER && e.received < since)
      lo = mid + 1;
    else
      hi = mid;
  }
  firstRecord_ = lo;
  lineStart_ = 0;
  lineIndex_ = firstRecord_;
  line_.clear();
  chunk_.clear();
  hasMark_ = false;
}

bool CamEventLog::recordAt(size_t index, CamEvent& e) {
  size_t inChunk = chunk_.size() / CAM_EVENT_RECORD;
  if (index < chunkFirst_ || index >= chunkFirst_ + inChunk) {  // read the next few records of that log in one go
    bool old = index < oldRecords_;
    size_t local = old ? index : index - oldRecords_, inFile = old ? oldRecords_ : allRecords_ - oldRecords_;
    size_t n = inFile - local < 16 ? inFile - local : 16;
    chunk_.resize(n * CAM_EVENT_RECORD);
    chunkFirst_ = index;
    if (!n || !storage_.readAt(old ? LOG_OLD : LOG, local * CAM_EVENT_RECORD, &chunk_[0], chunk_.size())) {
      chunk_.clear();
      return false;
    }
  }
  return camEventDecode((const uint8_t*)chunk_.data() + (index - chunkFirst_) * CAM_EVENT_RECORD, e);
}

size_t CamEventLog::csvRead(size_t offset, char* out, size_t len) {
  const size_t head = sizeof(CSV_HEADER) - 1, generated = head + legacyOld_ + legacyCur_;
  bool marked = false;
  size_t done = 0;
  while (done < len) {
    size_t at = offset + done, n;
    if (at < head) {
      n = len - done < head - at ? len - done : head - at;
      memcpy(out + done, CSV_HEADER + at, n);
    } else if (at < head + legacyOld_) {
      n = len - done < head + legacyOld_ - at ? len - done : head + legacyOld_ - at;
      if (!storage_.readAt(LEGACY_CSV_OLD, at - head, out + done, n)) return done;
    } else if (at < generated) {
      n = len - done < generated - at ? len - done : generated - at;
      if (!storage_.readAt(LEGACY_CSV, at - head - legacyOld_, out + done, n)) return done;
    } else {
      size_t g = at - generated;
      // A send cut short asks again from part way through the last piece, so each piece notes where it began.
      if (g < lineStart_) {
        if (hasMark_ && g >= markStart_) {
          lineStart_ = markStart_;
          lineIndex_ = markIndex_;
          line_ = markLine_;
        } else {
          lineStart_ = 0;
          lineIndex_ = firstRecord_;
          line_.clear();
        }
      }
      while (g >= lineStart_ + line_.size()) {
        lineStart_ += line_.size();
        line_.clear();
        if (lineIndex_ >= allRecords_) {  // the end of the history; read ahead again should a piece be asked for again
          std::string().swap(chunk_);
          return done;
        }
        CamEvent e;
        if (recordAt(lineIndex_++, e) && (csvSensor_.empty() || e.sensor == csvSensor_) && (!csvSince_ || e.received >= csvSince_))
          line_ = camEventCsvLine(e);
      }
      if (!marked) {
        markStart_ = lineStart_;
        markIndex_ = lineIndex_;
        markLine_ = line_;
        hasMark_ = marked = true;
      }
      n = len - done < lineStart_ + line_.size() - g ? len - done : lineStart_ + line_.size() - g;
      memcpy(out + done, line_.data() + (g - lineStart_), n);
    }
    done += n;
  }
  return done;
}
