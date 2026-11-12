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
  writeAlive(millis_());  // a planned restart then knows exactly when the hub stopped
}

// Rewritten every 10 minutes while the clock is set, so after a power cut the hub can say how long it was off.
void SensorCamServer::writeAlive(uint32_t now) {
  aliveAt_ = now;
  aliveWritten_ = true;
  int64_t t = (int64_t)time(nullptr);
  if (t >= (int64_t)CAM_CLOCK_VALID_AFTER) storage_.writeFile(ALIVE_FILE, (const char*)&t, sizeof(t));
}

// Logged once the network is up, and once the clock is set or has had long enough to be.
void SensorCamServer::noteHubStart(uint32_t now) {
  time_t t = time(nullptr);
  bool clockOk = t >= CAM_CLOCK_VALID_AFTER;
  if (startNoted_ || !networkSeen_ || (!clockOk && now - beganAt_ < START_CLOCK_WAIT_MS)) return;
  startNoted_ = true;
  bool offKnown = clockOk && lastAlive_ >= CAM_CLOCK_VALID_AFTER && t >= lastAlive_ && (int64_t)(t - lastAlive_) < (int64_t)CAM_AGE_UNKNOWN;
  uint32_t off = offKnown ? (uint32_t)(t - lastAlive_) : CAM_AGE_UNKNOWN;
  std::string reason = cfg_.startReason.empty() ? std::string("unknown") : cfg_.startReason;
  std::string message = cfg_.hostName + " started (" + reason + ")";
  if (offKnown) message += ", after being off for up to " + camDurationText(off);
  hubEvent(8, reason, off, message);
}

std::string camDurationText(uint32_t seconds) {
  uint32_t m = (seconds + 59) / 60;
  if (m < 60) return std::to_string(m) + " min";
  if (m < 48 * 60) return std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min";
  return std::to_string(m / 1440) + " days " + std::to_string(m / 60 % 24) + " h";
}

// The NVR is listening when it holds a subscription and the last event pushed to it arrived. With no NVR address set,
// any subscriber counts.
bool SensorCamServer::nvrListening() const {
  uint32_t nvr = cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str());
  for (int i = 0; i < SubTable::SIZE; i++) {
    const CamSub& s = subs_.at(i);
    if (s.used && (!nvr || s.peer == nvr) && !(s.push && s.pushFailed)) return true;
  }
  return false;
}

void SensorCamServer::setNvrWatch(bool on, uint32_t goneMs) {
  cfg_.watchNvr = on;
  nvrGoneMs_ = goneMs;
  nvrSeenAt_ = millis_();
  nvrGone_ = false;
  for (Health& h : health_) h.recordPending = false;  // alerts from before the change are not checked
}

void SensorCamServer::startAlert(int id, bool delayed, uint32_t now) {
  Health& h = health_[id - 1];
  h.recordPending = true;
  h.alertDelayed = delayed;
  h.alertAt = now;
}

// Each new alert is checked a few seconds after it starts, when its event push has had time to arrive, or when it ends
// if that is sooner. One the NVR did not get is logged and sent, because nothing else would show it is missing.
void SensorCamServer::checkNvr(uint32_t now) {
  bool listening = nvrListening();
  if (listening) {
    nvrSeenAt_ = now;
    if (nvrGone_) hubEvent(7, "", 0, "The NVR is listening to " + cfg_.hostName + " again");
    nvrGone_ = false;
  } else if (!nvrGone_ && now - nvrSeenAt_ >= nvrGoneMs_) {
    nvrGone_ = true;
    hubEvent(6, "", 0, "The NVR is not listening to " + cfg_.hostName + ": alerts are not being recorded");
  }
  for (int id = 1; id <= sensors_.count(); id++) {
    Health& h = health_[id - 1];
    if (!slots_[id - 1].used || !h.recordPending || (now - h.alertAt < RECORD_CHECK_MS && sensors_.active(id))) continue;
    h.recordPending = false;
    if (listening) continue;
    SensorReport r;
    r.battery = h.battery;
    r.active = true;
    logEvent(id, r, h.alertDelayed, 5);
    notifyHook(id, 5, h.alertDelayed);
  }
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

void SensorCamServer::logEvent(int id, const SensorReport& r, bool delayed, uint8_t kind) { addEvent(slotName(id), slots_[id - 1].name, r, delayed, kind); }

// The hub's own events: in the history as sensor "hub", and sent as node problems.
void SensorCamServer::hubEvent(uint8_t kind, const std::string& detail, uint32_t age, const std::string& message) {
  SensorReport r;
  r.age = age;
  addEvent("hub", detail, r, false, kind);
  notifyHubHook(kind, detail, message);
}

void SensorCamServer::addEvent(const std::string& sensor, const std::string& name, const SensorReport& r, bool delayed, uint8_t kind) {
  CamEvent& e = events_[eventNext_];
  e = CamEvent();
  e.received = time(nullptr);
  e.age = r.age;
  snprintf(e.sensor, sizeof(e.sensor), "%s", sensor.c_str());
  camCopyText(e.name, sizeof(e.name), name);
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

static const char* camEventText(uint8_t kind, bool active, bool delayed) {
  switch (kind) {
    case 1: return "went silent";
    case 2: return "low battery";
    case 3: return "alert in quiet hours";
    case 4: return "weak signal";
    case 5: return delayed ? "delayed alert not recorded: NVR not listening" : "alert not recorded: NVR not listening";
    case 6: return "NVR not listening";
    case 7: return "NVR listening again";
    case 8: return "hub started";
    default: return !active ? "clear" : delayed ? "delayed alert" : "alert";
  }
}

std::string SensorCamServer::csvLine(const CamEvent& e) {
  static const char* const DIRECTIONS[] = {"", "approaching", "moving away", "left to right", "right to left"};
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER, ageKnown = e.age != CAM_AGE_UNKNOWN;
  std::string what = camEventText(e.kind, e.active, e.delayed);
  if (e.kind == 8 && e.age != CAM_AGE_UNKNOWN) what += " after being off for up to " + camDurationText(e.age);
  if (e.kind == 8) ageKnown = false;  // its age is how long the hub was off, not when it happened
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
    if (!wasActive) startAlert(id, false, now);
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

// A late alert waits for its own NVR record; CamDelayedQueue decides which alerts share one.
void SensorCamServer::queueDelayed(int id, const SensorReport& r, uint32_t now) {
  const Slot& s = slots_[id - 1];
  bool known = r.age != CAM_AGE_UNKNOWN && r.age < 1000000;  // the millisecond clock covers 49 days
  delayed_.add(s.mac, s.input, reportDirection(r), known, known ? now - r.age * 1000 : now, holdFor(id));
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
  bool resend = h.counterSeen && h.counter == r.counter && h.counterActive == r.active && now - h.counterAt < RESEND_WINDOW_MS;
  if (!resend) {
    h.counterSeen = true;
    h.counter = r.counter;
    h.counterAt = now;
    h.counterActive = r.active;
    applyReport(id, r, now);
  }
  // Confirmed once it is logged, and again for a resend, whose sender evidently missed the first confirmation.
  if (cfg_.espNowSend) {
    std::string confirmation = camConfirmation(mac, r, cfg_.espNowKey);
    cfg_.espNowSend(mac, (const uint8_t*)confirmation.data(), (int)confirmation.size());
  }
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
  CamDelayed d;
  if (sensors_.anyActive() || delayedGap_ || !delayed_.take(d)) return;
  int id = findSlot(d.mac, d.input);
  if (id) {  // it may have been deleted while waiting
    sensors_.trigger(id, now, d.direction, true, holdFor(id));
    startAlert(id, true, now);
  }
  nextDelayedAt_ = now + (id ? holdFor(id) : cfg_.holdMs) + DELAYED_GAP_MS;
  delayedGap_ = true;
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && queryParam(target, "token") != cfg_.sensorToken) {
    status = 403;
    return camJsonError("bad token");
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
  return ok ? std::string(JSON_OK) : camJsonError("unknown sensor");
}

std::string SensorCamServer::statusJson(uint32_t now) {
  int known = 0;
  for (int id = 1; id <= sensors_.count(); id++) known += slots_[id - 1].used;
  return jsonObject([&](CamJson& j) {
    j.num("sensors", known).array("active");
    for (int id = 1; id <= sensors_.count(); id++)
      if (sensors_.active(id)) j.str(slotName(id));
    j.end().num("subscribers", subs_.count()).num("streams", streamCount()).num("delayed_waiting", delayed_.count()).array("heard");
    for (int id = 1; id <= sensors_.count(); id++) {
      const Health& h = health_[id - 1];
      if (!h.heard) continue;
      j.object().str("sensor", slotName(id)).num("last_heard_s", (now - h.heardAt) / 1000).num("battery", h.battery != CAM_BATTERY_UNKNOWN, h.battery).flag("silent", h.silent).end();
    }
    j.end().num("uptime_s", now / 1000);
  });
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
  std::string out;
  out.reserve((size_t)count * 200 + 8);
  CamJson j(out);
  j.array();
  for (int n = 0; n < count; n++) {
    const CamEvent& e = events_[(eventNext_ - 1 - n + 2 * EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
    bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
    bool ageKnown = e.age != CAM_AGE_UNKNOWN && e.kind != 8;  // a start's age is how long the hub was off
    j.object().str("sensor", e.sensor).str("name", e.name).num("kind", e.kind).flag("active", e.active).flag("delayed", e.delayed);
    j.str("received", clockOk, clockOk ? iso(e.received) : std::string());
    j.str("happened", clockOk && ageKnown, clockOk && ageKnown ? iso(e.received - (time_t)e.age) : std::string());
    j.num("age_s", ageKnown, e.age).num("battery", e.battery != CAM_BATTERY_UNKNOWN, e.battery);
    if (e.kind == 8) j.num("off_s", e.age != CAM_AGE_UNKNOWN, e.age);
    j.array("targets");
    for (int i = 0; i < e.targetCount; i++)
      j.object().num("distance_cm", e.targets[i].distanceCm != CAM_DISTANCE_UNKNOWN, e.targets[i].distanceCm).num("direction", e.targets[i].direction).end();
    j.end().end();
  }
  j.finish();
  return out;
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
