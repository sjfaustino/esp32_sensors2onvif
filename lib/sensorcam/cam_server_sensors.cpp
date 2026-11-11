// SensorCamServer: sensor reports, node health, the event history and the sensor table.
#include "cam_server.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"

// The history survives a restart. The whole file is rewritten, so a burst of events waits and is written once.
void SensorCamServer::saveEvents() {
  flushCsv();
  uint32_t header[4] = {0x31564543, (uint32_t)sizeof(CamEvent), (uint32_t)eventCount_, (uint32_t)eventNext_};
  eventsSaveFailed_ = !storage_.writeWith(EVENTS_FILE, [&](FILE* f) { return fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(events_, 1, sizeof(events_), f) == sizeof(events_); });
  eventsDirty_ = false;
  eventsSavedAt_ = millis_();
}

void SensorCamServer::saveNow() {
  if (eventsDirty_) saveEvents();
}

void SensorCamServer::loadEvents() {
  std::string blob;
  uint32_t header[4];
  if (!storage_.readFile(EVENTS_FILE, blob, sizeof(header) + sizeof(events_)) || blob.size() != sizeof(header) + sizeof(events_)) return;
  memcpy(header, blob.data(), sizeof(header));
  if (header[0] != 0x31564543 || header[1] != sizeof(CamEvent) || header[2] > (uint32_t)EVENT_LOG_SIZE || header[3] >= (uint32_t)EVENT_LOG_SIZE) return;
  memcpy((void*)events_, blob.data() + sizeof(header), sizeof(events_));
  eventCount_ = (int)header[2];
  eventNext_ = (int)header[3];
}

void SensorCamServer::healthAlert(int id, uint8_t kind, uint32_t now) {
  SensorReport r;
  r.battery = health_[id - 1].battery;
  logEvent(id, r, false, kind);
  notifyHook(id, kind, false);
  if (cfg_.healthAlarm) sensors_.trigger(id, now, 0, false, holdFor(id));
}

void SensorCamServer::logEvent(int id, const SensorReport& r, bool delayed, uint8_t kind) {
  CamEvent& e = events_[eventNext_];
  e.received = time(nullptr);
  e.age = r.age;
  snprintf(e.sensor, sizeof(e.sensor), "%s", slotName(id).c_str());
  camCopyText(e.name, sizeof(e.name), slots_[id - 1].name);
  e.kind = kind;
  e.active = r.active;
  e.delayed = delayed;
  e.battery = r.battery;
  e.targetCount = r.targetCount;
  for (int i = 0; i < CAM_MAX_TARGETS; i++) e.targets[i] = r.targets[i];
  eventNext_ = (eventNext_ + 1) % EVENT_LOG_SIZE;
  if (eventCount_ < EVENT_LOG_SIZE) eventCount_++;
  eventsDirty_ = true;
  csvPending_ += csvLine(e);
  if (csvPending_.size() > 4096) flushCsv();
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

std::string SensorCamServer::csvLine(const CamEvent& e) {
  static const char* const DIRECTIONS[] = {"", "approaching", "moving away", "left to right", "right to left"};
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER, ageKnown = e.age != CAM_AGE_UNKNOWN;
  const char* what = e.kind == 1 ? "went silent" : e.kind == 2 ? "low battery" : e.kind == 3 ? "alert in quiet hours" : e.kind == 4 ? "weak signal" : !e.active ? "clear" : e.delayed ? "delayed alert" : "alert";
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
         csvText(e.name) + "," + what + "," + (e.battery == CAM_BATTERY_UNKNOWN ? std::string() : std::to_string(e.battery)) + "," + distance + "," + direction + "\n";
}

// Added to rather than rewritten, so the history can grow far beyond the events kept in memory.
void SensorCamServer::flushCsv() {
  if (csvPending_.empty()) return;
  bool downloading = false;  // a download reads both files, so they must not change places under it
  for (const HttpConn& c : http_) downloading = downloading || (c.fd >= 0 && c.pull == &csvRead_);
  if (!downloading && storage_.fileSize(EVENTS_CSV) + csvPending_.size() > EVENTS_CSV_MAX) storage_.renameFile(EVENTS_CSV, EVENTS_CSV_OLD);
  // With storage full the lines are dropped, rather than kept and grown without limit.
  if (storage_.appendFile(EVENTS_CSV, csvPending_.data(), csvPending_.size()) || csvPending_.size() > 8192) csvPending_.clear();
}

uint32_t SensorCamServer::holdFor(int id) const {
  uint32_t seconds = slots_[id - 1].settings.holdS;
  return seconds ? seconds * 1000 : cfg_.holdMs;
}

bool SensorCamServer::quietNow(int id) const { return camQuiet(slots_[id - 1].settings, cfg_.localMinute ? cfg_.localMinute() : -1); }

bool SensorCamServer::applyReport(int id, const SensorReport& r, uint32_t now) {
  if (id < 1 || id > sensors_.count()) return false;
  Health& h = health_[id - 1];
  h.heard = true;
  h.heardAt = now;
  h.silent = false;
  bool batteryWentLow = false;
  if (r.battery != CAM_BATTERY_UNKNOWN) {  // a report without a reading must not erase the last one
    h.battery = r.battery;
    if (r.battery <= LOW_BATTERY_PERCENT && !h.lowBattery) {
      h.lowBattery = true;
      batteryWentLow = true;
    } else if (r.battery >= LOW_BATTERY_PERCENT + 5) {  // the gap stops a reading near the limit from alerting again and again
      h.lowBattery = false;
    }
  }

  uint32_t lateAfter = slots_[id - 1].node.lateS ? slots_[id - 1].node.lateS : DELAYED_AFTER_S;
  bool late = r.age > lateAfter;  // an unknown age counts as late
  bool wasActive = sensors_.active(id);
  bool wasReported = h.reported;
  h.reported = r.active;
  bool quiet = r.active && quietNow(id);  // logged, but the NVR is not told
  if (r.active && late && !quiet) {
    queueDelayed(id, r, now);
  } else if (r.active && !quiet) {
    // Starts the alert, or extends it, so activations closer together than the hold make one NVR record.
    sensors_.trigger(id, now, reportDirection(r), false, holdFor(id));
  }
  // A clear report does not end the alert early, for the same reason: the alert ends when its hold runs out.
  // Repeats and heartbeats that change nothing would push real events out of the log.
  // An activation is logged unless it only repeats one whose alert is still running; a sensor that never reports clear
  // still has each new alert logged.
  if (late || (r.active ? !wasReported || !wasActive : wasReported)) logEvent(id, r, late, quiet ? 3 : 0);
  if (r.active && !quiet && (late || !wasActive)) notifyHook(id, 0, late);
  if (batteryWentLow) healthAlert(id, 2, now);
  return true;
}

// A late alert waits for its own NVR record. One from the same sensor within the hold of a waiting one joins it, as it
// would have live. When the queue is full, it joins any waiting alert from that sensor rather than being lost.
void SensorCamServer::queueDelayed(int id, const SensorReport& r, uint32_t now) {
  const Slot& s = slots_[id - 1];
  bool known = r.age != CAM_AGE_UNKNOWN && r.age < 1000000;  // the millisecond clock covers 49 days
  uint32_t happened = known ? now - r.age * 1000 : now;
  DelayedAlert* same = nullptr;
  for (int n = 0; n < delayedCount_; n++) {
    DelayedAlert& d = delayed_[(delayedHead_ + n) % DELAYED_QUEUE_SIZE];
    if (d.input != s.input || memcmp(d.mac, s.mac, 6) != 0) continue;
    int32_t apart = (int32_t)(happened - d.happenedAt);
    if (known && d.timeKnown && (uint32_t)(apart < 0 ? -apart : apart) <= holdFor(id)) {
      same = &d;
      break;
    }
    if (delayedCount_ == DELAYED_QUEUE_SIZE) same = &d;
  }
  if (!same && delayedCount_ < DELAYED_QUEUE_SIZE) {
    same = &delayed_[(delayedHead_ + delayedCount_) % DELAYED_QUEUE_SIZE];
    *same = DelayedAlert();
    memcpy(same->mac, s.mac, 6);
    same->input = s.input;
    same->timeKnown = known;
    same->happenedAt = happened;
    delayedCount_++;
  }
  if (!same) return;  // the queue is full of other sensors' alerts; this one is still in the event history
  same->direction = reportDirection(r);
  if (known && same->timeKnown && (int32_t)(happened - same->happenedAt) > 0) same->happenedAt = happened;
}

void SensorCamServer::endAlerts() {
  for (int id = 1; id <= sensors_.count(); id++) sensors_.clear(id);
}

// Keeps the most recent senders, so a new sensor can be seen on the page before it has been added.
void SensorCamServer::noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi) {
  uint32_t now = millis_();
  UnknownSender* entry = nullptr;
  UnknownSender* spare = &unknown_[0];  // a free row, or else the one silent for longest
  for (UnknownSender& u : unknown_) {
    if (u.used && u.input == input && memcmp(u.mac, mac, 6) == 0) {
      entry = &u;
      break;
    }
    if (spare->used && (!u.used || now - u.heardAt > now - spare->heardAt)) spare = &u;
  }
  if (!entry) {
    entry = spare;
    *entry = UnknownSender();
    entry->used = true;
    memcpy(entry->mac, mac, 6);
    entry->input = input;
  }
  entry->rssi = rssi;
  entry->heardAt = now;
  entry->messages++;
}

void SensorCamServer::espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  SensorReport r;
  CamPacketCheck check = camCheckPacket(mac, data, len, cfg_.espNowKey, r);
  if (check == CAM_PACKET_INVALID) return;
  int id = findSlot(mac, r.input);
  if (id == 0) {
    noteUnknown(mac, r.input, rssi);
    return;
  }
  Health& h = health_[id - 1];
  // The report number is never a reason to refuse a correctly signed report: a sensor that restarts its count must
  // not lose an alert. The price is that a recorded report could be played back to make a false alert.
  const char* refusal = check == CAM_PACKET_BAD_SIGNATURE                    ? "wrong signature"
                        : check == CAM_PACKET_UNSIGNED && !cfg_.espNowUnsigned ? "not signed"
                                                                               : nullptr;
  if (refusal) {
    h.refused++;
    h.refusal = refusal;
    return;
  }
  noteSignal(id, rssi);
  // A resend repeats the report a moment later, and the hub ignores it. Only a short while, so a restarted sensor that
  // happens to reuse the last number is still heard.
  uint32_t now = millis_();
  if (h.counterSeen && h.counter == r.counter && h.counterActive == r.active && now - h.counterAt < RESEND_WINDOW_MS) return;
  h.counterSeen = true;
  h.counter = r.counter;
  h.counterAt = now;
  h.counterActive = r.active;
  applyReport(id, r, now);
}

// Smoothed, so a single weak packet does not count. Weak after a few, and strong again only with some margin.
void SensorCamServer::noteSignal(int id, int8_t rssi) {
  Health& h = health_[id - 1];
  h.rssi = rssi;
  if (rssi == 0) return;  // not measured
  h.rssiAvg8 = h.rssiCount ? (int16_t)(h.rssiAvg8 + (rssi * 8 - h.rssiAvg8) / 4) : (int16_t)(rssi * 8);
  if (h.rssiCount < 255) h.rssiCount++;
  if (!h.weak && h.rssiCount >= 3 && h.rssiAvg8 < WEAK_SIGNAL_DBM * 8) {
    h.weak = true;
    SensorReport r;
    r.battery = h.battery;
    logEvent(id, r, false, 4);
    notifyHook(id, 4, false);
  } else if (h.weak && h.rssiAvg8 > (WEAK_SIGNAL_DBM + 5) * 8) {
    h.weak = false;
  }
}

// Late alerts play back one at a time so each becomes its own NVR record.
void SensorCamServer::serviceDelayed(uint32_t now) {
  // The flag keeps the comparison valid: an old timestamp alone would look like the future after 24.8 days.
  if (delayedGap_ && (int32_t)(now - nextDelayedAt_) >= 0) delayedGap_ = false;
  if (delayedCount_ == 0 || sensors_.anyActive() || delayedGap_) return;
  DelayedAlert d = delayed_[delayedHead_];
  delayedHead_ = (delayedHead_ + 1) % DELAYED_QUEUE_SIZE;
  delayedCount_--;
  int id = findSlot(d.mac, d.input);
  if (id) sensors_.trigger(id, now, d.direction, true, holdFor(id));  // it may have been deleted while waiting
  nextDelayedAt_ = now + (id ? holdFor(id) : cfg_.holdMs) + DELAYED_GAP_MS;
  delayedGap_ = true;
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && queryParam(target, "token") != cfg_.sensorToken) {
    status = 403;
    return "{\"ok\":false,\"error\":\"bad token\"}\n";
  }
  uint8_t mac[6];
  std::string input = queryParam(target, "input");
  int id = camParseMac(queryParam(target, "mac"), mac) ? findSlot(mac, input.empty() ? 1 : (uint8_t)atoi(input.c_str())) : 0;
  std::string state = queryParam(target, "state");
  SensorReport r;
  r.active = !(state == "clear" || state == "off");
  std::string age = queryParam(target, "age");
  r.age = age == "unknown" ? CAM_AGE_UNKNOWN : (uint32_t)strtoul(age.c_str(), nullptr, 10);
  std::string battery = queryParam(target, "battery");
  if (!battery.empty()) r.battery = camBatteryPercent(atoi(battery.c_str()));
  std::string distance = queryParam(target, "distance"), direction = queryParam(target, "direction");
  if (!distance.empty() || !direction.empty()) {
    int d = atoi(direction.c_str());
    r.targetCount = 1;
    if (!distance.empty()) r.targets[0].distanceCm = (uint16_t)atoi(distance.c_str());
    r.targets[0].direction = (uint8_t)(d >= 0 && d < CAM_DIRECTIONS ? d : 0);
  }
  bool ok = applyReport(id, r, millis_());
  status = ok ? 200 : 404;
  return ok ? "{\"ok\":true}\n" : "{\"ok\":false,\"error\":\"unknown sensor\"}\n";
}

std::string SensorCamServer::statusJson(uint32_t now) {
  std::string active;
  int known = 0;
  for (int id = 1; id <= sensors_.count(); id++) {
    known += slots_[id - 1].used;
    if (sensors_.active(id)) active += (active.empty() ? "\"" : ",\"") + slotName(id) + "\"";
  }
  std::string health;
  for (int id = 1; id <= sensors_.count(); id++) {
    const Health& h = health_[id - 1];
    if (!h.heard) continue;
    uint32_t quiet = now - h.heardAt;
    health += (health.empty() ? "" : ",") + std::string("{\"sensor\":\"") + slotName(id) + "\",\"last_heard_s\":" + std::to_string(quiet / 1000) +
              ",\"battery\":" + (h.battery == CAM_BATTERY_UNKNOWN ? std::string("null") : std::to_string(h.battery)) +
              ",\"silent\":" + (h.silent ? "true" : "false") + "}";
  }
  return "{\"sensors\":" + std::to_string(known) + ",\"active\":[" + active + "],\"subscribers\":" +
         std::to_string(subs_.count()) + ",\"streams\":" + std::to_string(streamCount()) + ",\"delayed_waiting\":" + std::to_string(delayedCount_) +
         ",\"heard\":[" + health + "],\"uptime_s\":" + std::to_string(now / 1000) + "}\n";
}

void camCopyText(char* out, size_t size, const std::string& text) {
  if (!size) return;
  size_t n = text.size() < size - 1 ? text.size() : size - 1;
  if (n < text.size())
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;  // text[n] continues a character, so stop before that character starts
  memcpy(out, text.data(), n);
  out[n] = 0;
}

std::string SensorCamServer::eventsJson() {
  // Trimmed to what RAM allows, because running out is fatal. About 200 bytes each, doubled while building the reply.
  int count = eventCount_;
  while (count > 0 && !roomFor((size_t)count * 400)) count /= 2;
  std::string out = "[";
  out.reserve((size_t)count * 200 + 8);
  for (int n = 0; n < count; n++) {
    const CamEvent& e = events_[(eventNext_ - 1 - n + 2 * EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
    bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
    bool ageKnown = e.age != CAM_AGE_UNKNOWN;
    if (n) out += ",";
    out += std::string("{\"sensor\":\"") + e.sensor + "\",\"name\":\"" + camJsonEscape(e.name) + "\",\"kind\":" + std::to_string(e.kind) + ",\"active\":" + (e.active ? "true" : "false") +
           ",\"delayed\":" + (e.delayed ? "true" : "false") +
           ",\"received\":" + (clockOk ? "\"" + iso(e.received) + "\"" : std::string("null")) +
           ",\"happened\":" + (clockOk && ageKnown ? "\"" + iso(e.received - (time_t)e.age) + "\"" : std::string("null")) +
           ",\"age_s\":" + (ageKnown ? std::to_string(e.age) : std::string("null")) +
           ",\"battery\":" + (e.battery == CAM_BATTERY_UNKNOWN ? std::string("null") : std::to_string(e.battery)) + ",\"targets\":[";
    for (int i = 0; i < e.targetCount; i++) {
      out += std::string(i ? "," : "") + "{\"distance_cm\":" +
             (e.targets[i].distanceCm == CAM_DISTANCE_UNKNOWN ? std::string("null") : std::to_string(e.targets[i].distanceCm)) +
             ",\"direction\":" + std::to_string(e.targets[i].direction) + "}";
    }
    out += "]}";
  }
  return out + "]\n";
}

int SensorCamServer::findSlot(const uint8_t mac[6], uint8_t input) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && slots_[i].input == input && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

std::string SensorCamServer::slotName(int id) const {
  return camMacText(slots_[id - 1].mac) + "/" + std::to_string(slots_[id - 1].input);
}

bool SensorCamServer::sensorFits(const std::string& path) const {
  uint8_t mac[6];
  size_t slash = path.find('/');
  if (slash == std::string::npos || !camParseMac(path.substr(0, slash), mac)) return true;  // not inside a sensor's folder
  if (findSlot(mac, (uint8_t)atoi(path.c_str() + slash + 1))) return true;
  for (const Slot& s : slots_)
    if (!s.used) return true;
  return false;
}

void SensorCamServer::rescan() {
  bool seen[CAM_MAX_SENSORS] = {false};
  std::vector<CamFsEntry> nodes;
  storage_.list("", nodes);
  for (const CamFsEntry& s : nodes) {
    uint8_t mac[6];
    if (!s.dir || !camParseMac(s.name, mac)) continue;
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    CamNodeSettings node;  // a damaged file means the defaults
    std::string nodeText;
    if (!storage_.readFile(s.name + "/settings.txt", nodeText, 256) || !camParseNodeSettings(nodeText, node)) node = CamNodeSettings();
    for (const CamFsEntry& e : inputs) {
      if (!e.dir || camClassifyPath(s.name + "/" + e.name) != CAM_PATH_SENSOR) continue;
      uint8_t input = (uint8_t)atoi(e.name.c_str());
      int id = findSlot(mac, input);
      for (int i = 0; id == 0 && i < CAM_MAX_SENSORS; i++) {
        if (slots_[i].used) continue;
        slots_[i].used = true;
        memcpy(slots_[i].mac, mac, 6);
        slots_[i].input = input;
        health_[i] = Health();
        health_[i].watchedAt = millis_();
        id = i + 1;
      }
      if (id) {
        seen[id - 1] = true;
        std::string folder = s.name + "/" + e.name, name, text;
        slots_[id - 1].name = storage_.readFile(folder + "/name.txt", name, 64) ? name : std::string();
        CamSensorSettings settings;  // a damaged file means the defaults
        if (!storage_.readFile(folder + "/settings.txt", text, 256) || !camParseSensorSettings(text, settings)) settings = CamSensorSettings();
        slots_[id - 1].settings = settings;
        slots_[id - 1].node = node;
      }
    }
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    if (!slots_[i].used || seen[i]) continue;
    sensors_.clear(i + 1);
    slots_[i] = Slot();
    health_[i] = Health();
  }
  frameKey_.clear();
}
