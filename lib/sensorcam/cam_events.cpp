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
    default: return !active ? "clear" : delayed ? "delayed alert" : "alert";
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
  bool clockOk = e.received >= 1577836800, ageKnown = e.age != CAM_AGE_UNKNOWN;
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
    if (ok && !storage.writeFile(LOG, records.data(), records.size())) return;  // tried again at the next start
  }
  storage.removeTree(LEGACY_BIN);
}

void CamEventLog::load() {
  if (!storage_.exists(LOG) && !storage_.exists(LOG_OLD) && storage_.exists(LEGACY_BIN)) convertLegacy(storage_);
  // One pass over both logs, oldest first: the newest events stay in the ring, and each log's CSV size is measured.
  const char* const files[2] = {LOG_OLD, LOG};
  size_t* bytes[2] = {&oldBytes_, &curBytes_};
  size_t* csv[2] = {&oldCsvBytes_, &curCsvBytes_};
  std::string buf;
  for (int f = 0; f < 2; f++) {
    size_t size = storage_.fileSize(files[f]);
    *bytes[f] = size - size % CAM_EVENT_RECORD;  // a record cut short at the end is written over by the next one
    *csv[f] = 0;
    for (size_t at = 0; at < *bytes[f]; at += buf.size()) {
      size_t n = *bytes[f] - at < 32 * CAM_EVENT_RECORD ? *bytes[f] - at : 32 * CAM_EVENT_RECORD;
      buf.resize(n);
      if (!storage_.readAt(files[f], at, &buf[0], n)) break;
      for (size_t i = 0; i < n; i += CAM_EVENT_RECORD) {
        CamEvent e;
        if (!camEventDecode((const uint8_t*)buf.data() + i, e)) continue;
        remember(e);
        *csv[f] += camEventCsvLine(e).size();
      }
    }
  }
}

void CamEventLog::add(const CamEvent& e) {
  remember(e);
  uint8_t r[CAM_EVENT_RECORD];
  camEventEncode(e, r);
  if (pending_.size() >= PENDING_MAX) pending_.erase(0, CAM_EVENT_RECORD);
  pending_.append((const char*)r, sizeof(r));
}

bool CamEventLog::save(bool downloading) {
  if (pending_.empty()) return true;
  if (!downloading && curBytes_ + pending_.size() > FILE_MAX && storage_.renameFile(LOG, LOG_OLD)) {
    oldBytes_ = curBytes_;
    oldCsvBytes_ = curCsvBytes_;
    curBytes_ = curCsvBytes_ = 0;
  }
  saveFailed_ = !storage_.writeAt(LOG, curBytes_, pending_.data(), pending_.size());
  if (saveFailed_) return false;
  for (size_t i = 0; i < pending_.size(); i += CAM_EVENT_RECORD) {
    CamEvent e;
    if (camEventDecode((const uint8_t*)pending_.data() + i, e)) curCsvBytes_ += camEventCsvLine(e).size();
  }
  curBytes_ += pending_.size();
  pending_.clear();
  return true;
}

size_t CamEventLog::csvBegin() {
  legacyOld_ = storage_.fileSize(LEGACY_CSV_OLD);
  legacyCur_ = storage_.fileSize(LEGACY_CSV);
  oldRecords_ = oldBytes_ / CAM_EVENT_RECORD;
  allRecords_ = oldRecords_ + curBytes_ / CAM_EVENT_RECORD;
  lineStart_ = lineIndex_ = 0;
  line_.clear();
  chunk_.clear();
  hasMark_ = false;
  return sizeof(CSV_HEADER) - 1 + legacyOld_ + legacyCur_ + oldCsvBytes_ + curCsvBytes_;
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

bool CamEventLog::csvRead(size_t offset, char* out, size_t len) {
  const size_t head = sizeof(CSV_HEADER) - 1, generated = head + legacyOld_ + legacyCur_;
  bool marked = false;
  for (size_t done = 0; done < len;) {
    size_t at = offset + done, n;
    if (at < head) {
      n = len - done < head - at ? len - done : head - at;
      memcpy(out + done, CSV_HEADER + at, n);
    } else if (at < head + legacyOld_) {
      n = len - done < head + legacyOld_ - at ? len - done : head + legacyOld_ - at;
      if (!storage_.readAt(LEGACY_CSV_OLD, at - head, out + done, n)) return false;
    } else if (at < generated) {
      n = len - done < generated - at ? len - done : generated - at;
      if (!storage_.readAt(LEGACY_CSV, at - head - legacyOld_, out + done, n)) return false;
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
        if (lineIndex_ >= allRecords_) return false;  // asked for more than csvBegin said there was
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
  return true;
}
