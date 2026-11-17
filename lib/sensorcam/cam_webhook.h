// Messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy: alerts, node problems,
// the hub's own events, and a daily check. Queued, and sent one at a time in SensorCamServer's loop.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_config.h"
#include "cam_hook_queue.h"
#include "cam_onvif_events.h"
#include "cam_post.h"

class CamWebhookSender {
 public:
  // cfg holds the start-up settings, which never change; what the pages change later is the part's own. It and the others outlive the sender.
  CamWebhookSender(const CamConfig& cfg, const CamAlarm& alarm, const CamSensorTable& table, CamChip& chip, const CamOnvifEvents& events,
                   CamMemoryFailures& failures)
      : cfg_(cfg), alarm_(alarm), table_(table), chip_(&chip), events_(events), failures_(failures), settings_(cfg.webhook) {}
  // A sensor's event, and the hub's own; each queued if the settings say it is wanted. Running out of memory drops the
  // message, counted, and the alert goes on.
  void notify(int id, CamEventKind kind, bool delayed);
  void notifyHub(CamEventKind kind, const std::string& detail, const std::string& message);
  void queue(const std::string& message, const std::string& json);  // as text or JSON, as the settings say
  void dailyCheck(uint32_t now);                                    // queued at the minute the settings give
  void tick(uint32_t now, bool mayConnect);                         // starts the next message; mayConnect: a socket is free
  void watch(fd_set& rd, fd_set& wr, int& maxFd) const { post_.watch(rd, wr, maxFd); }
  void serve(fd_set& rd, fd_set& wr, uint32_t now);
  int fd() const { return post_.fd(); }
  const std::string& result() const { return hooks_.result(); }  // how the last message went; empty before the first
  std::string pageJson();
  const CamWebhook& settings() const { return settings_; }
  void setSettings(const CamWebhook& w) { settings_ = w; }  // a message already waiting goes to the new address

 private:
  void start(uint32_t now);

  const CamConfig& cfg_;
  const CamAlarm& alarm_;
  const CamSensorTable& table_;
  CamChip* chip_;
  const CamOnvifEvents& events_;  // whether the NVR is listening, for the daily check
  CamMemoryFailures& failures_;
  CamWebhook settings_;
  CamPost post_;
  CamHookQueue hooks_;
  bool dailySent_ = false;  // and when, so the minute it is due sends one
  uint32_t dailySentAt_ = 0;
};
