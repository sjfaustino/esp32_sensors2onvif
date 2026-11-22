// Messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy: alerts, node problems,
// the hub's own events, and a daily check. Queued, and sent one at a time in SensorCamServer's loop.
#pragma once
#include "cam_text.h"
#include <stdint.h>
#include <functional>
#include <map>
#include <memory>
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
      : cfg_(cfg), alarm_(alarm), table_(table), chip_(&chip), events_(events), failures_(failures), settings_(cfg.webhook) {
    camSetTextLanguage(settings_.language);
  }
  // A sensor's event, and the hub's own; each queued if the settings say it is wanted. Running out of memory drops the
  // message, counted, and the alert goes on.
  void notify(int id, CamEventKind kind, bool delayed);
  void notifyHub(CamEventKind kind, const std::string& detail, const std::string& message);
  // A message in the settings' format. title is what it is about, such as "alert" or "daily check", and priority runs
  // from 1, for news, to 5, for an alert: ntfy and Gotify show them on the phone.
  void queue(const std::string& title, int priority, const std::string& message, const std::string& json);
  void dailyCheck(uint32_t now);  // queued at the minute the settings give
  // At the day and minute the settings give, or now when forced: begins the past week's summary, which weeklyStep
  // reads from the history a little on each pass and then sends. False when the clock is not set, or there was no
  // memory for it.
  bool weeklySummary(uint32_t now, bool force);
  void weeklyStep(uint32_t now);
  void tick(uint32_t now, bool mayConnect);  // starts the next message; mayConnect: a socket is free
  void watch(fd_set& rd, fd_set& wr, int& maxFd) const { post_.watch(rd, wr, maxFd); }
  void serve(fd_set& rd, fd_set& wr, uint32_t now);
  int fd() const { return post_.fd(); }
  const std::string& result() const { return hooks_.result(); }  // how the last message went; empty before the first
  std::string pageJson();
  const CamWebhook& settings() const { return settings_; }
  void setSettings(const CamWebhook& w) {  // a message already waiting goes to the new address, as it was worded
    settings_ = w;
    camSetTextLanguage(w.language);  // the language of every message from here on, Home Assistant's too
  }

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
  bool weeklySent_ = false;
  uint32_t weeklySentAt_ = 0;
  struct WeeklyWork {  // a summary under way
    CamWeeklySummary s;
    CamHistoryScan scan;
    std::map<std::string, int> perSensor;
    std::map<std::string, std::string> names;
    time_t t = 0;
  };
  std::unique_ptr<WeeklyWork> weekly_;
  void weeklyFinish(WeeklyWork& w, uint32_t now);
};
