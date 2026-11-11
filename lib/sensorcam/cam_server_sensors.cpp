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
  if (cfg_.healthAlarm) sensors_.trigger(id, now);
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
}

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

  bool late = r.age > DELAYED_AFTER_S;  // an unknown age counts as late
  bool wasActive = sensors_.active(id);
  if (r.active && late) {
    if (delayedCount_ < DELAYED_QUEUE_SIZE) {
      DelayedAlert& d = delayed_[(delayedHead_ + delayedCount_) % DELAYED_QUEUE_SIZE];
      memcpy(d.mac, slots_[id - 1].mac, 6);
      d.input = slots_[id - 1].input;
      d.direction = reportDirection(r);
      delayedCount_++;
    }
  } else if (r.active) {
    sensors_.trigger(id, now, reportDirection(r));
  } else if (!late) {
    sensors_.clear(id);
  }
  // Repeats and heartbeats that change nothing would push real events out of the log.
  if (late || r.active != wasActive) logEvent(id, r, late);
  if (batteryWentLow) healthAlert(id, 2, now);  // last, because an inactive report would cancel the alarm straight away
  return true;
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

void SensorCamServer::espNowInput(const uint8_t mac[6], const SensorReport& r, int8_t rssi) {
  int id = findSlot(mac, r.input);
  if (id == 0) {
    noteUnknown(mac, r.input, rssi);
    return;
  }
  Health& h = health_[id - 1];
  h.rssi = rssi;
  // A resend repeats the counter. Only equality is rejected, so a sensor that restarts its count still works.
  if (h.counterSeen && h.counter == r.counter) return;
  h.counterSeen = true;
  h.counter = r.counter;
  applyReport(id, r, millis_());
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
  if (id) sensors_.trigger(id, now, d.direction, true);  // it may have been deleted while waiting
  nextDelayedAt_ = now + cfg_.holdMs + DELAYED_GAP_MS;
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
        std::string name;
        slots_[id - 1].name = storage_.readFile(s.name + "/" + e.name + "/name.txt", name, 64) ? name : std::string();
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
