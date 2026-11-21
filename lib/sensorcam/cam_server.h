// The hub in one non-blocking loop: sensor alerts in, over ESP-NOW and HTTP, and out to the NVR as ONVIF motion events
// and RTSP video, with the web pages, Home Assistant over MQTT, and a webhook. Each of those is its own part; this owns
// them, runs them, and passes on what one asks of another.
//
//   CamAlarm          the sensors, their reports and health, arming, the event history, and that each alert reached the NVR
//   CamNodes          pairing nodes, and the firmware files they update from
//   CamOnvifEvents    the NVR's event subscriptions and pushes, and WS-Discovery
//   CamVideo          the RTSP video and the picture it shows
//   CamWeb            HTTP: the ONVIF requests, the web pages and their API
//   CamMqttClient     Home Assistant
//   CamWebhookSender  messages to a web address of the user's choosing
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_auth.h"
#include "cam_config.h"
#include "cam_mqtt_client.h"
#include "cam_node_images.h"
#include "cam_nodes.h"
#include "cam_onvif_events.h"
#include "cam_sensor_table.h"
#include "cam_storage.h"
#include "cam_video.h"
#include "cam_web_server.h"
#include "cam_webhook.h"

class SensorCamServer : private CamAlarmOutside {
 public:
  SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn);
  bool begin();
  void poll(int maxWaitMs);  // call continuously
  void networkUp();          // call after each Wi-Fi (re)connect
  // Milliseconds since the socket wait last worked. A large value means the server has stopped serving.
  uint32_t stalledMs(uint32_t now) const { return now - lastGoodWait_; }
  std::string debugLine();                              // counters for the serial heartbeat
  void setOnvifPassword(const std::string& password) {  // for the PC tests
    web_.setOnvifPassword(password);
    video_.setOnvifPassword(password);
  }
  void espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi = 0);  // a raw ESP-NOW message; call from the same task as poll()
  void saveNow() { alarm_.saveNow(); }                                                     // writes any events not yet saved; call before a restart
  void networkLost(uint32_t seconds) { alarm_.networkLost(seconds); }  // call on rejoining Wi-Fi after being off it, without a restart
  bool updating() const { return web_.updating(); }                    // a firmware upload is under way
  std::string routes() { return web_.routesText(); }                   // for the PC tests
  void setDrainMs(uint32_t ms) { web_.setDrainMs(ms); }                // for the PC tests
  uint32_t nvrLetIn() const { return web_.nvrLetIn(); }                // times the NVR took the place of another connection
  uint32_t nvrVideoLetIn() const { return video_.nvrLetIn(); }         // the same for its video
  bool alarmBusy() const { return alarm_.busy(); }                     // see CamAlarm::busy
  bool nvrListeningNow() const { return nvrListening(millis_()); }     // see CamOnvifEvents::nvrListening
  uint32_t memoryFailures() const { return failures_.count(); }        // times memory ran out and only that piece of work was dropped
#ifdef CAM_TEST_HOOKS                                                  // only in the PC builds, for the tests
  CamAlarm& alarm() { return alarm_; }                                 // its test hooks, such as setMinuteMs, and its arm mode
#endif

 private:
  void housekeeping(uint32_t now);  // alerts, saving, the NVR and video: the part of poll() before waiting
  void pollOnce(int maxWaitMs);     // waits for the network, then serves the connections

  // What the alarm asks of the parts made after it.
  bool nvrListening(uint32_t now) const override;
  bool downloadingHistory() const override;
  void notifyHook(int id, CamEventKind kind, bool delayed) override;
  void notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) override;
  void sensorReset(int id) override;
  void sensorsRescanned(bool listChanged) override;

  const CamConfig cfg_;  // the start-up settings; see CamConfig for who holds what changes later
  CamChip* chip_;        // the board's parts, or ones with every default for a part left out
  CamRadio* radio_;
  CamStore* store_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  CamMemoryFailures failures_;
  uint32_t lastGoodWait_ = 0;
  uint32_t waitErrors_ = 0;
  int lastWaitErrno_ = 0;
  CamStorage storage_;
  CamLoginGuard logins_;  // wrong passwords, on the web page, ONVIF and RTSP
  // Each part is made after those it uses. The alarm, made first, reaches the others through CamAlarmOutside.
  CamNodeImages images_{storage_};        // the node firmware files, which the alarm and CamNodes both read
  CamSensorTable table_{cfg_, storage_};  // the sensors; the alarm changes it, the others read it
  CamAlarm alarm_;
  CamNodes nodes_;
  CamOnvifEvents events_;
  CamMqttClient mqtt_;
  CamVideo video_;
  CamWebhookSender webhook_;
  CamWeb web_;
};
