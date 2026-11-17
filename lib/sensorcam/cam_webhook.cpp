// CamWebhookSender: messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy.
#include "cam_webhook.h"
#include <string.h>
#include <time.h>
#include <new>
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

// Alerts are sent when alerts are chosen, node problems when problems are. An alert the NVR missed is sent if either is,
// because it means a recording is missing. A message there is no memory for is counted as dropped, and the alert goes on.
void CamWebhookSender::notify(int id, CamEventKind kind, bool delayed) {
  const CamWebhook& w = settings_;
  bool wanted = kind == CAM_EVENT_REPORT                                          ? w.alerts
                : kind == CAM_EVENT_NOT_DELIVERED || kind == CAM_EVENT_TEST_ALERT ? w.alerts || w.problems
                                                                                  : w.problems;
  if (w.url.empty() || !wanted) return;
  try {
    std::string event = camEventName(kind, true, delayed);
    std::string sensor = alarm_.slotName(id), name = alarm_.slotOf(id).name;
    std::string message = camEventMessage(kind, name.empty() ? sensor : name + " (" + sensor + ")", "", CAM_AGE_UNKNOWN, delayed);
    uint8_t battery = alarm_.healthOf(id).battery;
    time_t now = time(nullptr);
    bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
    queue(message, jsonObject([&](CamJson& j) {
            j.str("event", event).str("sensor", sensor).str("name", name).num("battery", battery != CAM_BATTERY_UNKNOWN, battery);
            j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
    hooks_.dropOne();
  }
}

// The hub's own events, sent with node problems, in the same shape as the event history.
void CamWebhookSender::notifyHub(CamEventKind kind, const std::string& detail, const std::string& message) {
  const CamWebhook& w = settings_;
  if (w.url.empty() || !w.problems) return;
  time_t now = time(nullptr);
  bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
  try {
    queue(message, jsonObject([&](CamJson& j) {
            j.str("event", camEventName(kind, true, false)).str("sensor", "hub").str("name", "").str("detail", detail).null("battery");
            j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
    hooks_.dropOne();
  }
}

void CamWebhookSender::queue(const std::string& message, const std::string& json) { hooks_.add(settings_.text ? message : json); }

void CamWebhookSender::tick(uint32_t now, bool mayConnect) {
  if (post_.idle() && hooks_.count() && mayConnect) start(now);
}

void CamWebhookSender::serve(fd_set& rd, fd_set& wr, uint32_t now) {
  if (!post_.idle() && post_.service(FD_ISSET(post_.fd(), &rd), FD_ISSET(post_.fd(), &wr), now))
    hooks_.noteResult(post_.result(), time(nullptr));  // last, as the one step that needs memory: running out then loses only the note
}

void CamWebhookSender::start(uint32_t now) {
  HttpUrl url;
  if (!parseHttpUrl(settings_.url, url)) {  // the address was removed while messages waited
    hooks_.clear();
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind; the message waits.
  const std::string& body = hooks_.oldest();
  std::string out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                    "\r\nContent-Type: " + (settings_.text ? "text/plain; charset=utf-8" : JSON_TYPE) +
                    "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  out.reserve(out.size() + body.size());
  out += body;
  CamPost::Start started = post_.start(url, out, CAM_HOOK_WAIT_MS, CAM_HOOK_WAIT_MS, now);
  if (started == CamPost::NO_SOCKET) return;  // still queued, so it is tried again on the next pass
  hooks_.takeOldest();
  if (started == CamPost::REFUSED) hooks_.noteResult("could not connect", time(nullptr));
}

std::string CamWebhookSender::pageJson() {
  const CamWebhook& w = settings_;
  bool clockOk = hooks_.resultAt() >= CAM_CLOCK_VALID_AFTER;
  return jsonObject([&](CamJson& j) {
    j.str("url", w.url).flag("text", w.text).flag("alerts", w.alerts).flag("problems", w.problems).num("daily", w.dailyMinute);
    j.str("last", hooks_.result()).str("lastAt", clockOk, clockOk ? iso(hooks_.resultAt()) : std::string());
    j.num("waiting", hooks_.count() + !post_.idle()).num("dropped", hooks_.dropped());
  });
}

// At the minute set under Notifications, local time: what the hub knows of its nodes, its alerts and the NVR, in one
// message. Needs the clock, so a hub whose clock is not set sends none, which is itself worth noticing.
void CamWebhookSender::dailyCheck(uint32_t now) {
  const CamWebhook& w = settings_;
  if (w.url.empty() || w.dailyMinute < 0 || chip_->localMinute() != w.dailyMinute) return;
  if (dailySent_ && now - dailySentAt_ < 2 * 60000) return;  // once in the minute it is due
  dailySent_ = true;
  dailySentAt_ = now;
  time_t t = time(nullptr);
  try {
    CamDailyCheck d;
    d.hub = cfg_.hostName;
    d.mode = camArmModeName(alarm_.armMode());
    d.nvrListening = nvrListening_(now);
    for (int n = 1; n <= CAM_MAX_SENSORS; n++) {
      if (!alarm_.nodeAt(n).used) continue;
      d.nodes++;
      bool heard = false;
      for (int id = 1; id <= CAM_MAX_SENSORS && !heard; id++)
        heard = alarm_.slotOf(id).used && alarm_.slotOf(id).node == n && alarm_.healthOf(id).heard && now - alarm_.healthOf(id).heardAt < 86400000u;
      d.heard += heard;
    }
    for (int id = 1; id <= alarm_.board().count(); id++) {
      const CamAlarm::Health& h = alarm_.healthOf(id);
      if (!alarm_.slotOf(id).used) continue;
      const char* problem = h.silent ? "silent" : h.lowBattery ? "low battery" : h.weak ? "weak signal" : h.refused ? "messages refused" : nullptr;
      if (problem) d.problems.push_back((alarm_.slotOf(id).name.empty() ? alarm_.slotName(id) : alarm_.slotOf(id).name) + ": " + problem);
    }
    d.allCounted = false;
    for (int n = 0; n < alarm_.log().count(); n++) {
      const CamEvent& e = alarm_.log().recent(n);
      if (e.kind == CAM_EVENT_TEST_ALERT && d.testAgoS < 0 && e.received <= t) d.testAgoS = (long long)(t - e.received);
      if (e.received < t - 86400) {
        d.allCounted = true;  // an older event is kept, so every one of the last day is too
        continue;
      }
      d.alerts += e.kind == CAM_EVENT_REPORT && e.active;
      d.notDelivered += e.kind == CAM_EVENT_NOT_DELIVERED;
    }
    if (alarm_.log().count() < CamEventLog::RECENT) d.allCounted = true;  // nothing has been dropped from memory yet
    std::string message = camDailyMessage(d);
    queue(message, jsonObject([&](CamJson& j) {
            j.str("event", "daily check").str("sensor", "hub").str("name", "").str("time", iso(t)).str("hub", cfg_.hostName);
            j.num("nodes", d.nodes).num("heard", d.heard).array("problems");
            for (const std::string& p : d.problems) j.str(p);
            j.end().num("alerts", d.alerts).flag("allCounted", d.allCounted).num("notDelivered", d.notDelivered);
            j.flag("nvrListening", d.nvrListening).str("mode", d.mode).num("testAgo_s", d.testAgoS >= 0, d.testAgoS);
            j.str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
    hooks_.dropOne();
  }
}
