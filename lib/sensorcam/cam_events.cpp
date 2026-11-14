#include "cam_events.h"
#include <stdio.h>
#include <string.h>
#include "cam_soap.h"

static const char LOG[] = "events.log";
static const char LOG_OLD[] = "events-old.log";
static const char LEGACY_CSV[] = "events.csv";          // the history of firmware before this log; read, never written
static const char LEGACY_CSV_OLD[] = "events-old.csv";
static const char LEGACY_BIN[] = "events.bin";          // that firmware's newest events, converted once
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

std::string camEventMessage(uint8_t kind, const std::string& who, const std::string& detail, uint32_t offS, bool delayed) {
  bool known = offS != CAM_AGE_UNKNOWN;
  switch (kind) {
    case CAM_EVENT_NOT_DELIVERED: return who + ": " + camEventName(kind, true, delayed) + ", because the NVR is not listening";
    case CAM_EVENT_NVR_GONE: return "The NVR is not listening to " + who + ": alerts are not reaching it";
    case CAM_EVENT_NVR_BACK: return "The NVR is listening to " + who + " again";
    case CAM_EVENT_HUB_STARTED:
      return who + " started (" + (detail.empty() ? std::string("unknown") : detail) + ")" + (known ? ", after being off for up to " + camDurationText(offS) : "");
    case CAM_EVENT_NETWORK_LOST:
      return who + " was off its Wi-Fi network for up to " + (known ? camDurationText(offS) : std::string("an unknown time")) +
             "; alerts in that time reach the NVR as past events once it is back";
    case CAM_EVENT_ARMED: return who + " was armed" + (detail.empty() ? std::string() : " from " + detail);
    case CAM_EVENT_RESTART_LOOP: return who + " has restarted by itself " + detail + ": something keeps stopping it, so alerts may be missed";
    case CAM_EVENT_ARMED_HOME:
      return who + " was armed home" + (detail.empty() ? std::string() : " from " + detail) + ": interior sensors are logged but not sent to the NVR";
    case CAM_EVENT_DISARMED:
      return who + " was disarmed" + (detail.empty() ? std::string() : " from " + detail) + ": alerts are logged but not sent to the NVR";
    default: return who + ": " + camEventName(kind, true, delayed);
  }
}

std::string camDurationText(uint32_t seconds) {
  uint32_t m = (seconds + 59) / 60;
  if (m < 60) return std::to_string(m) + " min";
  if (m < 48 * 60) return std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min";
  return std::to_string(m / 1440) + " days " + std::to_string(m / 60 % 24) + " h";
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
  if (e.offS != CAM_AGE_UNKNOWN && (e.kind == CAM_EVENT_HUB_STARTED || e.kind == CAM_EVENT_NETWORK_LOST)) what += " for up to " + camDurationText(e.offS);
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
  return (clockOk ? iso(e.received) : std::string()) + "," + (clockOk && ageKnown ? iso(e.received - (time_t)e.age) : std::string()) + "," + e.sensor + "," +
         csvText(e.text) + "," + what + "," + (e.battery == CAM_BATTERY_UNKNOWN ? std::string() : std::to_string(e.battery)) + "," + distance + "," + direction + "\n";
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
    bool ok = header[0] == 0x31564543 && (size == sizeof(CamEvent) || size == OLD_SIZE) && blob.size() == sizeof(header) + size * CamEventLog::RECENT &&
              header[2] <= (uint32_t)CamEventLog::RECENT && header[3] < (uint32_t)CamEventLog::RECENT;
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

void CamEventLog::add(const CamEvent& e) {
  uint8_t r[CAM_EVENT_RECORD];
  camEventEncode(e, r);
  if (pending_.size() >= PENDING_MAX) pending_.erase(0, CAM_EVENT_RECORD);
  pending_.append((const char*)r, sizeof(r));  // first, so an event shown is one that will be saved
  remember(e);
}

bool CamEventLog::save(bool downloading) {
  if (pending_.empty()) return true;
  if (!downloading && curBytes_ + pending_.size() > FILE_MAX && storage_.renameFile(LOG, LOG_OLD)) {
    oldBytes_ = curBytes_;
    curBytes_ = 0;
  }
  saveFailed_ = !storage_.writeAt(LOG, curBytes_, pending_.data(), pending_.size());
  if (saveFailed_) return false;
  curBytes_ += pending_.size();
  pending_.clear();
  if (pending_.capacity() > PENDING_KEPT) {  // grown while storage refused: given back, keeping the usual room
    std::string().swap(pending_);
    pending_.reserve(PENDING_KEPT);
  }
  return true;
}

void CamEventLog::csvBegin() {
  legacyOld_ = storage_.fileSize(LEGACY_CSV_OLD);
  legacyCur_ = storage_.fileSize(LEGACY_CSV);
  oldRecords_ = oldBytes_ / CAM_EVENT_RECORD;
  allRecords_ = oldRecords_ + curBytes_ / CAM_EVENT_RECORD;
  lineStart_ = lineIndex_ = 0;
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
          lineStart_ = lineIndex_ = 0;
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
        if (recordAt(lineIndex_++, e)) line_ = camEventCsvLine(e);
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
