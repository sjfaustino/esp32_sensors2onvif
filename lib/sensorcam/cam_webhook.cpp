// CamWebhookSender: messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy.
#include "cam_webhook.h"
#include "cam_timing.h"
#include <string.h>
#include <time.h>
#include <map>
#include <new>
#include "cam_net.h"
#include "cam_rtp.h"
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
    std::string sensor = table_.slotName(id), name = table_.slotOf(id).name;
    std::string message = camEventMessage(kind, name.empty() ? sensor : name + " (" + sensor + ")", "", CAM_AGE_UNKNOWN, delayed);
    const std::string& camera = table_.slotOf(id).settings.camera;
    if (!camera.empty()) message += "; look at " + camera;
    int priority = kind == CAM_EVENT_REPORT || kind == CAM_EVENT_NOT_DELIVERED ? 5 : kind == CAM_EVENT_TEST_ALERT ? 3 : 4;
    uint8_t battery = table_.healthOf(id).battery;
    time_t now = chip_->wallTime();
    bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
    queue(event + ": " + (name.empty() ? sensor : name), priority, message, jsonObject([&](CamJson& j) {
            j.str("event", event).str("sensor", sensor).str("name", name).num("battery", battery != CAM_BATTERY_UNKNOWN, battery);
            j.str("camera", camera);
            j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    failures_.note();
    hooks_.dropOne();
  }
}

// The hub's own events, sent with node problems, in the same shape as the event history.
void CamWebhookSender::notifyHub(CamEventKind kind, const std::string& detail, const std::string& message) {
  const CamWebhook& w = settings_;
  if (w.url.empty() || !w.problems) return;
  time_t now = chip_->wallTime();
  bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
  // Arming and disarming are news; the rest of the hub's events are problems.
  int priority = kind == CAM_EVENT_ARMED || kind == CAM_EVENT_ARMED_HOME || kind == CAM_EVENT_DISARMED || kind == CAM_EVENT_NVR_BACK ? 2 : 4;
  try {
    queue(camEventName(kind, true, false), priority, message, jsonObject([&](CamJson& j) {
            j.str("event", camEventName(kind, true, false)).str("sensor", "hub").str("name", "").str("detail", detail).null("battery");
            j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    failures_.note();
    hooks_.dropOne();
  }
}

// Header lines can carry only plain text, so a title with anything else goes as an RFC 2047 word, which ntfy reads.
static std::string headerText(const std::string& s) {
  bool plain = true;
  for (char ch : s) plain = plain && (unsigned char)ch >= 0x20 && (unsigned char)ch < 0x7f;
  if (plain) return s;
  std::string clean;  // control characters, which could end the header, go
  for (char ch : s)
    if ((unsigned char)ch >= 0x20 && ch != 0x7f) clean += ch;
  return "=?UTF-8?B?" + base64((const uint8_t*)clean.data(), clean.size()) + "?=";
}

// Each waiting message is its extra header lines, a zero byte, and its body, so sending it needs nothing else.
void CamWebhookSender::queue(const std::string& title, int priority, const std::string& message, const std::string& json) {
  const CamWebhook& w = settings_;
  std::string full = cfg_.hostName + ": " + title;
  if (w.format == CAM_HOOK_NTFY) {
    static const char* const TAGS[] = {"", "information_source", "information_source", "white_check_mark", "warning", "rotating_light"};
    std::string head = "Title: " + headerText(full) + "\r\nPriority: " + std::to_string(priority) + "\r\nTags: " + TAGS[priority] + "\r\n";
    if (!w.token.empty()) head += "Authorization: Bearer " + w.token + "\r\n";
    hooks_.add(head + '\0' + message);
  } else if (w.format == CAM_HOOK_GOTIFY) {
    std::string head = w.token.empty() ? std::string() : "X-Gotify-Key: " + w.token + "\r\n";
    hooks_.add(head + '\0' + jsonObject([&](CamJson& j) { j.str("title", full).str("message", message).num("priority", priority * 2); }));
  } else {
    hooks_.add(std::string(1, '\0') + (w.format == CAM_HOOK_TEXT ? message : json));
  }
}

void CamWebhookSender::tick(uint32_t now, bool mayConnect) {
  if (post_.idle() && hooks_.count() && mayConnect) start(now);
}

void CamWebhookSender::serve(fd_set& rd, fd_set& wr, uint32_t now) {
  if (!post_.idle() && post_.service(FD_ISSET(post_.fd(), &rd), FD_ISSET(post_.fd(), &wr), now))
    hooks_.noteResult(post_.result(), chip_->wallTime());  // last, as the one step that needs memory: running out then loses only the note
}

void CamWebhookSender::start(uint32_t now) {
  HttpUrl url;
  if (!parseHttpUrl(settings_.url, url)) {  // the address was removed while messages waited
    hooks_.clear();
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind; the message waits.
  const std::string& item = hooks_.oldest();
  size_t split = item.find('\0');
  if (split == std::string::npos) split = 0;  // never so, as queue() puts one in every message
  const char* body = item.data() + split + 1;
  size_t bodyLen = item.size() - split - 1;
  bool text = bodyLen == 0 || body[0] != '{';  // JSON always starts so, and a text message never does
  std::string out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                    "\r\nContent-Type: " + (text ? "text/plain; charset=utf-8" : JSON_TYPE) + "\r\nContent-Length: " + std::to_string(bodyLen) +
                    "\r\nConnection: close\r\n";
  out.append(item.data(), split);
  out += "\r\n";
  out.reserve(out.size() + bodyLen);
  out.append(body, bodyLen);
  CamPost::Start started = post_.start(url, out, CAM_HOOK_WAIT_MS, CAM_HOOK_WAIT_MS, now);
  if (started == CamPost::NO_SOCKET) return;  // still queued, so it is tried again on the next pass
  hooks_.takeOldest();
  if (started == CamPost::REFUSED) hooks_.noteResult("could not connect", chip_->wallTime());
}

std::string CamWebhookSender::pageJson() {
  const CamWebhook& w = settings_;
  bool clockOk = hooks_.resultAt() >= CAM_CLOCK_VALID_AFTER;
  return jsonObject([&](CamJson& j) {
    static const char* const FORMATS[] = {"json", "text", "ntfy", "gotify"};
    j.str("url", w.url).str("format", FORMATS[w.format]).flag("token", !w.token.empty()).flag("alerts", w.alerts).flag("problems", w.problems);
    j.num("daily", w.dailyMinute).num("weekday", w.weeklyDay).num("weekminute", w.weeklyMinute);
    j.str("last", hooks_.result()).str("lastAt", clockOk, clockOk ? iso(hooks_.resultAt()) : std::string());
    j.num("waiting", hooks_.count() + !post_.idle()).num("dropped", hooks_.dropped());
  });
}

// At the minute set under Notifications, local time: what the hub knows of its nodes, its alerts and the NVR, in one
// message. Needs the clock, so a hub whose clock is not set sends none, which is itself worth noticing.
void CamWebhookSender::dailyCheck(uint32_t now) {
  const CamWebhook& w = settings_;
  if (w.url.empty() || w.dailyMinute < 0 || chip_->localMinute() != w.dailyMinute) return;
  if (dailySent_ && now - dailySentAt_ < CAM_DAILY_ONCE_MS) return;  // once in the minute it is due
  dailySent_ = true;
  dailySentAt_ = now;
  time_t t = chip_->wallTime();
  try {
    CamDailyCheck d;
    d.hub = cfg_.hostName;
    d.mode = camArmModeName(alarm_.armMode());
    d.nvrListening = events_.nvrListening(now);
    for (int n = 1; n <= CAM_MAX_SENSORS; n++) {
      if (!table_.nodeAt(n).used) continue;
      d.nodes++;
      bool heard = false;
      for (int id = 1; id <= CAM_MAX_SENSORS && !heard; id++)
        heard = table_.slotOf(id).used && table_.slotOf(id).node == n && table_.healthOf(id).heard && now - table_.healthOf(id).heardAt < 86400000u;
      d.heard += heard;
    }
    for (int id = 1; id <= alarm_.board().count(); id++) {
      const CamSensorTable::Health& h = table_.healthOf(id);
      if (!table_.slotOf(id).used) continue;
      const char* problem = h.silent ? "silent" : h.lowBattery ? "low battery" : h.weak ? "weak signal" : h.refused ? "messages refused" : nullptr;
      if (problem) d.problems.push_back((table_.slotOf(id).name.empty() ? table_.slotName(id) : table_.slotOf(id).name) + ": " + problem);
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
    queue("daily check", 2, message, jsonObject([&](CamJson& j) {
            j.str("event", "daily check").str("sensor", "hub").str("name", "").str("time", iso(t)).str("hub", cfg_.hostName);
            j.num("nodes", d.nodes).num("heard", d.heard).array("problems");
            for (const std::string& p : d.problems) j.str(p);
            j.end().num("alerts", d.alerts).flag("allCounted", d.allCounted).num("notDelivered", d.notDelivered);
            j.flag("nvrListening", d.nvrListening).str("mode", d.mode).num("testAgo_s", d.testAgoS >= 0, d.testAgoS);
            j.str("message", message);
          }));
  } catch (const std::bad_alloc&) {
    failures_.note();
    hooks_.dropOne();
  }
}

// Who a node is in a message: its first sensor's name, or its address.
static std::string nodeLabel(const CamSensorTable& table, int node) {
  for (int id = 1; id <= CAM_MAX_SENSORS; id++)
    if (table.slotOf(id).used && table.slotOf(id).node == node && !table.slotOf(id).name.empty()) return table.slotOf(id).name;
  char mac[18];
  const uint8_t* m = table.nodeAt(node).mac;
  snprintf(mac, sizeof(mac), "%02X-%02X-%02X-%02X-%02X-%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return mac;
}

// The past week from the history on storage, which goes back further than the events kept in memory. Read once a week,
// up to 2000 events, a few records at a time.
bool CamWebhookSender::weeklySummary(uint32_t now, bool force) {
  const CamWebhook& w = settings_;
  if (w.url.empty()) return false;
  if (!force) {
    if (w.weeklyDay < 0 || chip_->localWeekday() != w.weeklyDay || chip_->localMinute() != w.weeklyMinute) return false;
    if (weeklySent_ && now - weeklySentAt_ < CAM_DAILY_ONCE_MS) return false;  // once in the minute it is due
    weeklySent_ = true;
    weeklySentAt_ = now;
  }
  time_t t = chip_->wallTime();
  if (t < CAM_CLOCK_VALID_AFTER) return false;
  try {
    CamWeeklySummary s;
    s.hub = cfg_.hostName;
    s.mode = camArmModeName(alarm_.armMode());
    s.nvrListening = events_.nvrListening(now);
    std::map<std::string, int> perSensor;
    std::map<std::string, std::string> names;
    static const uint8_t PROBLEMS[] = {CAM_EVENT_SILENT,        CAM_EVENT_LOW_BATTERY, CAM_EVENT_WEAK_SIGNAL,
                                       CAM_EVENT_NOT_DELIVERED, CAM_EVENT_NVR_GONE,    CAM_EVENT_NETWORK_LOST,
                                       CAM_EVENT_RESTART_LOOP,  CAM_EVENT_NODE_FAULT,  CAM_EVENT_BATTERY_RUNNING_OUT,
                                       CAM_EVENT_JAMMING,       CAM_EVENT_BUSY_SENSOR};
    s.allCounted = alarm_.log().eachSince(t - 7 * 86400, 2000, [&](const CamEvent& e) {
      if (e.kind == CAM_EVENT_REPORT && e.active) {
        s.alerts++;
        int& count = perSensor[e.sensor];
        count++;
        if (!names.count(e.sensor)) names[e.sensor] = e.text[0] ? std::string(e.text) : std::string(e.sensor);
      }
      s.notDelivered += e.kind == CAM_EVENT_NOT_DELIVERED;
      s.held += e.kind == CAM_EVENT_QUIET_ALERT || e.kind == CAM_EVENT_DISARMED_ALERT || e.kind == CAM_EVENT_HOME_ALERT;
      if (e.kind == CAM_EVENT_TEST_ALERT) {
        s.tests++;
        if (s.testAgoS < 0 && e.received <= t) s.testAgoS = (long long)(t - e.received);
      }
      for (uint8_t k : PROBLEMS) s.problems += e.kind == k;
    });
    for (const auto& p : perSensor)
      if (p.second > s.busiestAlerts) {
        s.busiestAlerts = p.second;
        s.busiest = names[p.first];
      }
    for (int n = 1; n <= CAM_MAX_SENSORS; n++) {
      const CamSensorTable::Node& node = table_.nodeAt(n);
      if (node.used && node.daysLeft >= 0 && node.daysLeft < 30)
        s.batteries.push_back(nodeLabel(table_, n) + ": about " + std::to_string(node.daysLeft) + (node.daysLeft == 1 ? " day" : " days"));
    }
    for (int id = 1; id <= alarm_.board().count(); id++) {
      const CamSensorTable::Health& h = table_.healthOf(id);
      if (!table_.slotOf(id).used) continue;
      const char* problem = h.silent ? "silent" : h.lowBattery ? "low battery" : h.weak ? "weak signal" : h.refused ? "messages refused" : nullptr;
      if (problem) s.nowProblems.push_back((table_.slotOf(id).name.empty() ? table_.slotName(id) : table_.slotOf(id).name) + ": " + problem);
    }
    std::string message = camWeeklyMessage(s);
    queue("weekly summary", 2, message, jsonObject([&](CamJson& j) {
            j.str("event", "weekly summary").str("sensor", "hub").str("name", "").str("time", iso(t)).str("hub", cfg_.hostName);
            j.num("alerts", s.alerts).flag("allCounted", s.allCounted).num("notDelivered", s.notDelivered).num("held", s.held);
            j.num("tests", s.tests).num("problems", s.problems).str("busiest", s.busiest).num("busiestAlerts", s.busiestAlerts);
            j.array("batteries");
            for (const std::string& b : s.batteries) j.str(b);
            j.end().array("problemsNow");
            for (const std::string& p : s.nowProblems) j.str(p);
            j.end().flag("nvrListening", s.nvrListening).str("mode", s.mode).num("testAgo_s", s.testAgoS >= 0, s.testAgoS);
            j.str("message", message);
          }));
    return true;
  } catch (const std::bad_alloc&) {
    failures_.note();
    hooks_.dropOne();
    return false;
  }
}
