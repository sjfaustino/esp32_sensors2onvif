// SensorCamServer: messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy.
#include "cam_server.h"
#include <string.h>
#include <time.h>
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

// Alerts are sent when alerts are chosen, node problems when problems are. An alert the NVR missed is sent if either is,
// because it means a recording is missing.
void SensorCamServer::notifyHook(int id, CamEventKind kind, bool delayed) {
  const CamWebhook& w = cfg_.webhook;
  bool wanted = kind == CAM_EVENT_REPORT ? w.alerts : kind == CAM_EVENT_NOT_RECORDED ? w.alerts || w.problems : w.problems;
  if (w.url.empty() || !wanted) return;
  std::string event = camEventName(kind, true, delayed);
  std::string sensor = slotName(id), name = slots_[id - 1].name;
  std::string message = (name.empty() ? sensor : name + " (" + sensor + ")") + ": " + event;
  if (kind == CAM_EVENT_NOT_RECORDED) message += ", because the NVR is not listening";
  uint8_t battery = health_[id - 1].battery;
  time_t now = time(nullptr);
  bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
  queueHook(message, jsonObject([&](CamJson& j) {
              j.str("event", event).str("sensor", sensor).str("name", name).num("battery", battery != CAM_BATTERY_UNKNOWN, battery);
              j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
            }));
}

// The hub's own events, sent with node problems, in the same shape as the event history.
void SensorCamServer::notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) {
  const CamWebhook& w = cfg_.webhook;
  if (w.url.empty() || !w.problems) return;
  time_t now = time(nullptr);
  bool clockOk = now >= CAM_CLOCK_VALID_AFTER;
  queueHook(message, jsonObject([&](CamJson& j) {
              j.str("event", camEventName(kind, true, false)).str("sensor", "hub").str("name", "").str("detail", detail).null("battery");
              j.str("time", clockOk, clockOk ? iso(now) : std::string()).str("hub", cfg_.hostName).str("message", message);
            }));
}

// One at a time, oldest first. A full queue drops the new message, so a burst cannot use up memory.
void SensorCamServer::queueHook(const std::string& message, const std::string& json) {
  if (hookCount_ == HOOK_QUEUE) {
    hookDropped_++;
    return;
  }
  hookQueue_[(hookHead_ + hookCount_) % HOOK_QUEUE] = cfg_.webhook.text ? message : json;
  hookCount_++;
}

void SensorCamServer::startHook(uint32_t now) {
  HttpUrl url;
  if (!parseHttpUrl(cfg_.webhook.url, url)) {  // the address was removed while messages waited
    for (std::string& waiting : hookQueue_) waiting.clear();
    hookCount_ = 0;
    return;
  }
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;  // still queued, so it is tried again on the next pass
  std::string body;
  body.swap(hookQueue_[hookHead_]);
  hookHead_ = (hookHead_ + 1) % HOOK_QUEUE;
  hookCount_--;
  netNonblock(fd);
  hook_ = NotifyIo();
  hook_.hook = true;
  hook_.fd = fd;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(url.port);
  to.sin_addr.s_addr = inet_addr(url.host.c_str());
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    endNotify(hook_, "could not connect");
    return;
  }
  hook_.out = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) + "\r\nContent-Type: " +
              (cfg_.webhook.text ? "text/plain; charset=utf-8" : JSON_TYPE) + "\r\nContent-Length: " + std::to_string(body.size()) +
              "\r\nConnection: close\r\n\r\n" + body;
  hook_.state = NotifyIo::CONNECTING;
  hook_.deadline = now + HOOK_WAIT_MS;
}

std::string SensorCamServer::webhookJson() {
  const CamWebhook& w = cfg_.webhook;
  bool clockOk = hookResultAt_ >= CAM_CLOCK_VALID_AFTER;
  return jsonObject([&](CamJson& j) {
    j.str("url", w.url).flag("text", w.text).flag("alerts", w.alerts).flag("problems", w.problems);
    j.str("last", hookResult_).str("lastAt", clockOk, clockOk ? iso(hookResultAt_) : std::string());
    j.num("waiting", hookCount_ + (hook_.state != NotifyIo::IDLE)).num("dropped", hookDropped_);
  });
}
