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
    j.object()
        .num("distance_cm", e.targets[i].distanceCm != CAM_DISTANCE_UNKNOWN, e.targets[i].distanceCm)
        .num("direction", e.targets[i].direction)
        .end();
  j.end().end();
}

// Measured an event at a time first, then built at its exact size, so it needs one block no larger than itself.
std::string SensorCamServer::eventsJson(bool* whole) {
  int count = alarm_.log().count() < CamEventLog::RECENT ? alarm_.log().count() : CamEventLog::RECENT;
  size_t sizes[CamEventLog::RECENT];
  std::string one;
  for (int n = 0; n < count; n++) {
    one.clear();
    eventJson(one, alarm_.log().recent(n));
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
    eventJson(out, alarm_.log().recent(n));
  }
  out += "]\n";
  return out;
}

std::string SensorCamServer::eventsTag() const {
  uint32_t h = 2166136261u;  // FNV-1a over the newest event: with the count of events added, it changes with the list
  if (alarm_.log().count()) {
    uint8_t r[CAM_EVENT_RECORD];
    camEventEncode(alarm_.log().recent(0), r);
    for (uint8_t b : r) h = (h ^ b) * 16777619u;
  }
  char tag[32];
  snprintf(tag, sizeof(tag), "\"%u-%08x\"", (unsigned)alarm_.log().added(), (unsigned)h);
  return tag;
}

// The overview page: what someone checking the hub wants at a glance.
std::string SensorCamServer::overviewJson(uint32_t now) {
  long long left = alarm_.disarmSecondsLeft(now);
  CamArmMode mode = armMode();
  const CamEvent* last = nullptr;  // the newest alert, whatever became of it
  for (int n = 0; n < alarm_.log().count() && !last; n++) {
    const CamEvent& e = alarm_.log().recent(n);
    bool alert = (e.kind == CAM_EVENT_REPORT && e.active) || e.kind == CAM_EVENT_QUIET_ALERT || e.kind == CAM_EVENT_DISARMED_ALERT ||
                 e.kind == CAM_EVENT_HOME_ALERT || e.kind == CAM_EVENT_TEST_ALERT || e.kind == CAM_EVENT_NOT_DELIVERED;
    if (alert) last = &e;
  }
  return jsonObject([&](CamJson& j) {
    j.str("mode", camArmModeName(mode)).flag("timed", mode == CAM_DISARMED && alarm_.disarmedUntil());
    j.num("left_s", left >= 0, left).array("active");
    for (int id = 1; id <= alarm_.board().count(); id++)
      if (alarm_.slotOf(id).used && alarm_.board().active(id))
        j.object().str("sensor", alarm_.slotName(id)).str("name", alarm_.slotOf(id).name).flag("past", alarm_.board().pastEvent(id)).end();
    j.end().object("nvr").flag("set", !cfg_.nvrIp.empty()).flag("listening", nvrListening(now)).num("streams", streamCount()).end();
    j.str("mqtt", mqtt_.state()).object("webhook").flag("set", !cfg_.webhook.url.empty()).str("last", hooks_.result()).end().array("problems");
    for (int id = 1; id <= alarm_.board().count(); id++) {
      const Health& h = alarm_.healthOf(id);
      if (!alarm_.slotOf(id).used) continue;
      const char* problem = h.silent ? "silent" : h.lowBattery ? "low battery" : h.weak ? "weak signal" : h.refused ? "messages refused" : nullptr;
      if (problem) j.object().str("sensor", alarm_.slotName(id)).str("name", alarm_.slotOf(id).name).str("problem", problem).end();
    }
    j.end();
    if (last) {
      bool clockOk = last->received >= CAM_CLOCK_VALID_AFTER;
      j.object("last").str("sensor", last->sensor).str("name", last->text).str("event", camEventName(last->kind, last->active, last->delayed));
      j.str("received", clockOk, clockOk ? iso(last->received) : std::string()).end();
    } else {
      j.null("last");
    }
    j.num("dropped", memoryFailures()).num("uptime_s", now / 1000);
    // For the Overview's checklist: what must be right before the hub can be trusted.
    time_t t = time(nullptr), lastTest = alarm_.lastTestAt();
    for (int n = 0; n < alarm_.log().count(); n++)
      if (alarm_.log().recent(n).kind == CAM_EVENT_TEST_ALERT && alarm_.log().recent(n).received > lastTest)
        lastTest = alarm_.log().recent(n).received;
    bool clockOk = t >= CAM_CLOCK_VALID_AFTER, heard = false;
    for (int id = 1; id <= alarm_.board().count(); id++) heard = heard || (alarm_.slotOf(id).used && alarm_.healthOf(id).heard);
    bool setupPassword = !cfg_.setupPasswordHash.empty() && camSameText(camSha1(cfg_.webPassword), cfg_.setupPasswordHash);
    bool testKnown = clockOk && lastTest >= CAM_CLOCK_VALID_AFTER && lastTest <= t;
    j.object("ready").flag("password", !cfg_.webPassword.empty() && !setupPassword).flag("clock", clockOk).flag("heard", heard);
    j.flag("notify", !cfg_.webhook.url.empty() || !cfg_.mqtt.host.empty()).flag("daily", !cfg_.webhook.url.empty() && cfg_.webhook.dailyMinute >= 0);
    j.num("testAgo_s", testKnown, (long long)(t - lastTest)).end();
  });
}

std::string SensorCamServer::statusJson(uint32_t now) {
  int known = 0;
  for (int id = 1; id <= alarm_.board().count(); id++) known += alarm_.slotOf(id).used;
  return jsonObject([&](CamJson& j) {
    j.num("sensors", known).array("active");
    for (int id = 1; id <= alarm_.board().count(); id++)
      if (alarm_.board().active(id)) j.str(alarm_.slotName(id));
    j.end().num("subscribers", subs_.count()).num("streams", streamCount()).num("delayed_waiting", alarm_.delayedCount()).array("heard");
    for (int id = 1; id <= alarm_.board().count(); id++) {
      const Health& h = alarm_.healthOf(id);
      if (!h.heard) continue;
      j.object()
          .str("sensor", alarm_.slotName(id))
          .num("last_heard_s", (now - h.heardAt) / 1000)
          .num("battery", h.battery != CAM_BATTERY_UNKNOWN, h.battery)
          .flag("silent", h.silent)
          .end();
    }
    j.end().num("uptime_s", now / 1000).flag("armed", armed()).str("mode", camArmModeName(armMode()));
  });
}
