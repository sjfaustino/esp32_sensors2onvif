// The hub's MQTT client, through which Home Assistant finds the hub and its sensors by itself. One connection to the
// broker, in SensorCamServer's loop: the server waits on its socket and calls service() and tick().
#pragma once
#include <stdint.h>
#include <time.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_config.h"
#include "cam_onvif_events.h"

class CamMqttClient {
 public:
  // cfg holds the start-up settings, which never change; what the pages change later is the part's own. It and the others outlive the client.
  CamMqttClient(const CamConfig& cfg, CamAlarm& alarm, const CamSensorTable& table, CamChip& chip, const CamOnvifEvents& events,
                CamMemoryFailures& failures, std::function<std::string()> localIp)
      : cfg_(cfg), alarm_(alarm), table_(table), chip_(&chip), events_(events), failures_(failures), localIp_(localIp), settings_(cfg.mqtt) {}
  void begin() { eventsSent_ = alarm_.log().added(); }  // after the history is read: the run before sent those
  // Connects, gives up on a broker that stopped answering, and builds the next message. mayConnect: a socket is free for
  // a new connection. Running out of memory throws, and leaves the message to be built on a later pass.
  void tick(uint32_t now, bool mayConnect);
  int fd() const { return io_.fd; }  // the connection's socket, or -1 for none
  bool wantsRead() const { return io_.fd >= 0 && io_.state != Io::CONNECTING; }
  bool wantsWrite() const { return io_.fd >= 0 && (io_.state == Io::CONNECTING || io_.outPos < io_.out.size()); }
  void service(bool readable, bool writable, uint32_t now);
  const CamMqtt& settings() const { return settings_; }
  void setSettings(const CamMqtt& m, uint32_t now);  // applied at once, with a new connection
  void sensorReset(int id) { shown_[id - 1] = Shown(); }
  void sensorsChanged() { io_.discovery = 0; }  // Home Assistant is told of the sensors again
  std::string state() const;                    // as the pages show it
  std::string pageJson();                       // the MQTT page's

 private:
  struct Shown {  // a sensor's state as last published, so only a change is sent
    bool told = false;
    bool alert = false, silent = false, low = false, weak = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    int8_t rssi = 0;
    uint32_t at = 0;
  };
  struct Told {  // a sensor Home Assistant was told of, so one deleted or replaced is taken away again
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
  };
  struct Io {
    enum State { OFF, CONNECTING, WAIT_CONNACK, READY } state = OFF;
    int fd = -1;
    std::string out;
    size_t outPos = 0;
    uint8_t in[64];
    size_t inLen = 0;
    size_t skip = 0;  // bytes of a packet too large for in, read and dropped
    uint32_t deadline = 0, sentAt = 0, heardAt = 0;
    uint16_t nextId = 1;
    uint16_t ackId = 0;     // the event sent and not yet acknowledged; 0 for none
    uint32_t ackEvent = 0;  // its number, counting the events added to the log since the start
    uint32_t ackSince = 0;
    bool online = false;
    int discovery = 0;  // the next discovery message; -1 once all are sent
    bool hubTold = false, hubAlert = false, hubNvr = false, hubArmed = false, hubHome = false;
    int64_t hubUntil = 0;
    bool oldSwitchGone = false;  // the Armed switch of earlier firmware, taken away from Home Assistant
    int subscribed = 0;          // topics subscribed to: the alarm panel's, then the test buttons'
    bool healthTold = false;
    uint32_t healthAt = 0;
  };
  void start(uint32_t now);
  void stop(const char* why, uint32_t now);
  void packet(const uint8_t* p, size_t len, uint32_t now);
  bool nextMessage(uint32_t now, std::string& out);  // the next message to send, if any
  bool discoveryStep(int step, std::string& out);
  bool sensorState(int i, bool alertsOnly, uint32_t now, std::string& out);
  bool eventMessage(std::string& out);
  std::string topicBase() const;  // the hub's topics start with this

  const CamConfig& cfg_;
  CamAlarm& alarm_;
  const CamSensorTable& table_;
  CamChip* chip_;
  const CamOnvifEvents& events_;  // whether the NVR is listening
  CamMemoryFailures& failures_;
  std::function<std::string()> localIp_;
  CamMqtt settings_;
  Io io_;
  Shown shown_[CAM_MAX_SENSORS];
  Told told_[CAM_MAX_SENSORS];
  uint32_t eventsSent_ = 0;  // the events published, counting as CamEventLog::added does
  uint32_t eventsLost_ = 0;  // more waited than the log keeps in memory
  uint32_t retryAt_ = 0;
  bool waiting_ = false;  // a connection failed, and the next waits until retryAt_
  std::string result_ = "not set up";
  time_t connectedAt_ = 0;
};
