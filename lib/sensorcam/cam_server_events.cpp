// SensorCamServer: the event history, and what the Events, Overview and status pages read.
#include "cam_server.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

// Written straight into the event, without using the heap, so an alert is logged even when memory has run out.
void SensorCamServer::logEvent(int id, const SensorReport& r, bool delayed, CamEventKind kind) {
  const Slot& s = slots_[id - 1];
  char sensor[sizeof(CamEvent::sensor)];
  snprintf(sensor, sizeof(sensor), "%02X-%02X-%02X-%02X-%02X-%02X/%u", s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5], (unsigned)s.input);
  addEvent(sensor, s.name.c_str(), r, delayed, kind);
}

// The hub's own events: in the history as sensor "hub", and sent as node problems.
void SensorCamServer::hubEvent(CamEventKind kind, const std::string& detail, uint32_t offS, const std::string& message, bool flag) {
  SensorReport r;  // age 0: it happened when it was logged
  r.active = flag;
  addEvent("hub", detail.c_str(), r, false, kind, offS);
  notifyHubHook(kind, detail, message);
}

void SensorCamServer::addEvent(const char* sensor, const char* text, const SensorReport& r, bool delayed, CamEventKind kind, uint32_t offS) {
  CamEvent e;
  e.received = time(nullptr);
  e.age = r.age;
  snprintf(e.sensor, sizeof(e.sensor), "%s", sensor);
  camCopyText(e.text, sizeof(e.text), text);
  e.offS = offS;
  e.kind = kind;
  e.active = r.active;
  e.delayed = delayed;
  e.battery = r.battery;
  e.targetCount = r.targetCount;
  for (int i = 0; i < CAM_MAX_TARGETS; i++) e.targets[i] = r.targets[i];
  log_.add(e);
}

void camCopyText(char* out, size_t size, const std::string& text) { camCopyText(out, size, text.c_str()); }

void camCopyText(char* out, size_t size, const char* text) {
  if (!size) return;
  size_t len = strlen(text);
  size_t n = len < size - 1 ? len : size - 1;
  if (n < len)
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;  // text[n] continues a character, so stop before that character starts
  memcpy(out, text, n);
  out[n] = 0;
}

// One event of the Events page's list.
static void eventJson(std::string& out, const CamEvent& e) {
  CamJson j(out);
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
  bool ageKnown = e.age != CAM_AGE_UNKNOWN;
  bool hub = strcmp(e.sensor, "hub") == 0;
  j.object().str("sensor", e.sensor).str("name", hub ? "" : e.text).str("event", camEventName(e.kind, e.active, e.delayed));
  if (hub) j.str("detail", e.text);
  j.num("kind", e.kind).flag("active", e.active).flag("delayed", e.delayed);
  j.str("received", clockOk, clockOk ? iso(e.received) : std::string());
  j.str("happened", clockOk && ageKnown, clockOk && ageKnown ? iso(e.received - (time_t)e.age) : std::string());
  j.num("age_s", ageKnown, e.age).num("battery", e.battery != CAM_BATTERY_UNKNOWN, e.battery);
  if (e.kind == CAM_EVENT_HUB_STARTED || e.kind == CAM_EVENT_NETWORK_LOST) j.num("off_s", e.offS != CAM_AGE_UNKNOWN, e.offS);
  j.array("targets");
  for (int i = 0; i < e.targetCount; i++)
    j.object().num("distance_cm", e.targets[i].distanceCm != CAM_DISTANCE_UNKNOWN, e.targets[i].distanceCm).num("direction", e.targets[i].direction).end();
  j.end().end();
}

// Measured an event at a time first, then built at its exact size, so it needs one block no larger than itself.
std::string SensorCamServer::eventsJson(bool* whole) {
  int count = log_.count() < CamEventLog::RECENT ? log_.count() : CamEventLog::RECENT;
  size_t sizes[CamEventLog::RECENT];
  std::string one;
  for (int n = 0; n < count; n++) {
    one.clear();
    eventJson(one, log_.recent(n));
    sizes[n] = one.size();
  }
  auto total = [&](int shown) {  // the brackets, the line break and the commas too
    size_t bytes = 3;
    for (int n = 0; n < shown; n++) bytes += sizes[n] + (n ? 1 : 0);
    return bytes;
  };
  int shown = count;
  while (shown > 0 && !roomFor(total(shown))) shown /= 2;  // the newest that fit in the memory there is
  if (whole) *whole = shown == count;
  std::string out;
  out.reserve(total(shown));
  out += '[';
  for (int n = 0; n < shown; n++) {
    if (n) out += ',';
    eventJson(out, log_.recent(n));
  }
  out += "]\n";
  return out;
}

std::string SensorCamServer::eventsTag() const {
  uint32_t h = 2166136261u;  // FNV-1a over the newest event: with the count of events added, it changes with the list
  if (log_.count()) {
    uint8_t r[CAM_EVENT_RECORD];
    camEventEncode(log_.recent(0), r);
    for (uint8_t b : r) h = (h ^ b) * 16777619u;
  }
  char tag[32];
  snprintf(tag, sizeof(tag), "\"%u-%08x\"", (unsigned)log_.added(), (unsigned)h);
  return tag;
}

// The overview page: what someone checking the hub wants at a glance.
std::string SensorCamServer::overviewJson(uint32_t now) {
  long long left = disarmSecondsLeft(now);
  CamArmMode mode = armMode();
  const CamEvent* last = nullptr;  // the newest alert, whatever became of it
  for (int n = 0; n < log_.count() && !last; n++) {
    const CamEvent& e = log_.recent(n);
    bool alert = (e.kind == CAM_EVENT_REPORT && e.active) || e.kind == CAM_EVENT_QUIET_ALERT || e.kind == CAM_EVENT_DISARMED_ALERT ||
                 e.kind == CAM_EVENT_HOME_ALERT || e.kind == CAM_EVENT_TEST_ALERT || e.kind == CAM_EVENT_NOT_DELIVERED;
    if (alert) last = &e;
  }
  return jsonObject([&](CamJson& j) {
    j.str("mode", camArmModeName(mode)).flag("timed", mode == CAM_DISARMED && cfg_.disarmedUntil);
    j.num("left_s", left >= 0, left).array("active");
    for (int id = 1; id <= sensors_.count(); id++)
      if (slots_[id - 1].used && sensors_.active(id)) j.object().str("sensor", slotName(id)).str("name", slots_[id - 1].name).flag("past", sensors_.pastEvent(id)).end();
    j.end().object("nvr").flag("set", !cfg_.nvrIp.empty()).flag("listening", nvrListening(now)).num("streams", streamCount()).end();
    j.str("mqtt", mqttState()).object("webhook").flag("set", !cfg_.webhook.url.empty()).str("last", hookResult_).end().array("problems");
    for (int id = 1; id <= sensors_.count(); id++) {
      const Health& h = health_[id - 1];
      if (!slots_[id - 1].used) continue;
      const char* problem = h.silent ? "silent" : h.lowBattery ? "low battery" : h.weak ? "weak signal" : h.refused ? "messages refused" : nullptr;
      if (problem) j.object().str("sensor", slotName(id)).str("name", slots_[id - 1].name).str("problem", problem).end();
    }
    j.end();
    if (last) {
      bool clockOk = last->received >= CAM_CLOCK_VALID_AFTER;
      j.object("last").str("sensor", last->sensor).str("name", last->text).str("event", camEventName(last->kind, last->active, last->delayed));
      j.str("received", clockOk, clockOk ? iso(last->received) : std::string()).end();
    } else {
      j.null("last");
    }
    j.num("dropped", memoryFailures_).num("uptime_s", now / 1000);
  });
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
    j.end().num("uptime_s", now / 1000).flag("armed", armed()).str("mode", camArmModeName(armMode()));
  });
}
